/*
  Brainfuck frontend for GCC

  Copyright (C) 2009 Free Software Foundation, Inc.
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/* Written by Giuseppe Scrivano <gscrivano@gnu.org>  */

#include "config.h"
#include "system.h"
#include "ansidecl.h"
#include "coretypes.h"
#include "opts.h"
#include "tree.h"
#include "tree-iterator.h"
#include "gimple.h"
#include "ggc.h"
#include "toplev.h"
#include "debug.h"
#include "options.h"
#include "flags.h"
#include "convert.h"
#include "diagnostic.h"
#include "langhooks.h"
#include "langhooks-def.h"
#include "target.h"
#include "cgraph.h"

void finish_file (void);
const char * brainfuck_printable_name (tree decl ATTRIBUTE_UNUSED,
                                       int kind ATTRIBUTE_UNUSED);
int brainfuck_gimplify_expr (tree *expr_p ATTRIBUTE_UNUSED,
                             gimple_seq *pre_p ATTRIBUTE_UNUSED,
                             gimple_seq *post_p ATTRIBUTE_UNUSED);

#undef LANG_HOOKS_NAME
#undef LANG_HOOKS_INIT
#undef LANG_HOOKS_INIT_OPTIONS
#undef LANG_HOOKS_HANDLE_OPTION
#undef LANG_HOOKS_POST_OPTIONS
#undef LANG_HOOKS_PARSE_FILE
#undef LANG_HOOKS_TYPE_FOR_MODE
#undef LANG_HOOKS_TYPE_FOR_SIZE
#undef LANG_HOOKS_BUILTIN_FUNCTION
#undef LANG_HOOKS_GLOBAL_BINDINGS_P
#undef LANG_HOOKS_PUSHDECL
#undef LANG_HOOKS_GETDECLS
#undef LANG_HOOKS_WRITE_GLOBALS
#undef LANG_HOOKS_GIMPLIFY_EXPR
#undef LANG_HOOKS_DECL_PRINTABLE_NAME

#define LANG_HOOKS_NAME "GNU brainfuck"
#define LANG_HOOKS_INIT brainfuck_init
#define LANG_HOOKS_DECL_PRINTABLE_NAME brainfuck_printable_name
#define LANG_HOOKS_GIMPLIFY_EXPR brainfuck_gimplify_expr
#define LANG_HOOKS_GETDECLS		brainfuck_langhook_getdecls
#define LANG_HOOKS_BUILTIN_FUNCTION	brainfuck_langhook_builtin_function
#define LANG_HOOKS_TYPE_FOR_MODE        brainfuck_langhook_type_for_mode
#define LANG_HOOKS_TYPE_FOR_SIZE        brainfuck_langhook_type_for_size
#define LANG_HOOKS_GLOBAL_BINDINGS_P    brainfuck_langhook_global_bindings_p
#define LANG_HOOKS_PUSHDECL             brainfuck_langhook_pushdecl
#define LANG_HOOKS_GETDECLS		brainfuck_langhook_getdecls
#define LANG_HOOKS_INIT_OPTIONS		brainfuck_langhook_init_options
#define LANG_HOOKS_HANDLE_OPTION	brainfuck_langhook_handle_option
#define LANG_HOOKS_POST_OPTIONS		brainfuck_langhook_post_options
#define LANG_HOOKS_PARSE_FILE		brainfuck_langhook_parse_file
#define LANG_HOOKS_WRITE_GLOBALS	brainfuck_langhook_write_globals


static bool
brainfuck_init (void)
{
  build_common_tree_nodes (false, false);

  /* The sizetype may be "unsigned long" or "unsigned long long".  */
  if (TYPE_MODE (long_unsigned_type_node) == ptr_mode)
    size_type_node = long_unsigned_type_node;
  else if (TYPE_MODE (long_long_unsigned_type_node) == ptr_mode)
    size_type_node = long_long_unsigned_type_node;
  else
    size_type_node = long_unsigned_type_node;
  set_sizetype (size_type_node);

  build_common_tree_nodes_2 (0);
  build_common_builtin_nodes ();

  void_list_node = build_tree_list (NULL_TREE, void_type_node);

  return true;
}

static unsigned int
brainfuck_langhook_init_options (unsigned int argc ATTRIBUTE_UNUSED,
                                 const char** argv ATTRIBUTE_UNUSED)
{
  return CL_Brainfuck;
}

static int
brainfuck_langhook_handle_option (size_t scode ATTRIBUTE_UNUSED,
                                  const char *arg ATTRIBUTE_UNUSED,
                                  int value ATTRIBUTE_UNUSED)
{
  return 0;
}

static bool
brainfuck_langhook_post_options (const char **pfilename ATTRIBUTE_UNUSED)
{
  flag_excess_precision_cmdline = EXCESS_PRECISION_FAST;
  return false;
}

static tree
in (tree pointer)
{
  tree res;
  tree fntype = build_function_type_list (integer_type_node,
                                          NULL_TREE);

  tree declf = build_decl (UNKNOWN_LOCATION, FUNCTION_DECL,
                           get_identifier ("getchar"), fntype);

  TREE_PUBLIC (declf) = 1;
  DECL_EXTERNAL (declf) = 1;

  pointer = fold_convert (char_type_node, pointer);

  res = build_call_expr (declf, 0);

  return build2 (MODIFY_EXPR, char_type_node, pointer,
                 fold_convert (char_type_node, res));
}

static tree
out (tree pointer)
{
  tree fntype = build_function_type_list (integer_type_node,
                                          integer_type_node,
                                          NULL_TREE);

  tree declf = build_decl (UNKNOWN_LOCATION, FUNCTION_DECL,
                           get_identifier ("putchar"), fntype);

  pointer = fold_convert (integer_type_node, pointer);

  return build_call_expr (declf, 1, pointer);
}

static tree
add_ptr (tree data, int delta)
{
  tree cst = build_int_cst (TREE_TYPE (data), delta);
  return build2 (PREINCREMENT_EXPR, TREE_TYPE (data), data, cst);
}

static tree
add (tree data, int delta)
{
  tree cst = build_int_cst (TREE_TYPE (data), delta);
  return build2 (PREINCREMENT_EXPR, TREE_TYPE (data), data, cst);
}

static tree
read_tree (FILE *finput, tree header, tree deref)
{
  char input;
  tree child;
  tree func = NULL_TREE;
  tree exit, body = NULL_TREE;

  while (fread (&input, 1, 1, finput))
    {
      switch (input)
        {
        case '>':
          append_to_statement_list (add_ptr (header, 1), &func);
          break;

        case '<':
          append_to_statement_list (add_ptr (header, -1), &func);
          break;

        case '+':
          append_to_statement_list (add (deref, 1), &func);
          break;

        case '-':
          append_to_statement_list (add (deref, -1), &func);
          break;

        case '.':
          append_to_statement_list (out (deref), &func);
          break;

        case ',':
          append_to_statement_list (in (deref), &func);
          break;

        case '[':
          child = read_tree (finput, header, deref);
          exit = build1 (EXIT_EXPR, char_type_node, 
                         build3 (COND_EXPR, char_type_node,
                                 deref,
                                 build_int_cst (char_type_node, 0),
                                 build_int_cst (char_type_node, 1)));

          body = build2 (COMPOUND_EXPR, char_type_node, 
                         exit, child);
          append_to_statement_list (build1 (LOOP_EXPR, char_type_node, body),
                                    &func);
          break;

        case ']':
          return func;

        default:
          /* Silently ignore everything else.  They can be comment or typos
             that will bring you to mental insanity.  */
          continue;
        }
    }

  return func;
}

static void
brainfuck_langhook_parse_file (int set_yy_debug ATTRIBUTE_UNUSED)
{
  FILE *finput = NULL;
  tree func = NULL_TREE;
  tree decl = build_decl (input_location, FUNCTION_DECL,
                          get_identifier ("main"),
                          build_function_type (integer_type_node,
                                               void_list_node));
  tree data = build_decl (input_location, VAR_DECL, get_identifier ("data"),
                          build_vector_type (char_type_node, 32768));
  tree header = build_decl (input_location, VAR_DECL, get_identifier ("header"),
                            build_pointer_type (char_type_node));
  tree deref = fold_build1 (INDIRECT_REF, char_type_node, header);
  tree assign = build2 (INIT_EXPR, build_pointer_type (char_type_node), header,
                        fold_build1 (ADDR_EXPR, build_pointer_type (char_type_node),
                                     data));

  append_to_statement_list (assign, &func);

  finput = fopen (main_input_filename, "r");

  append_to_statement_list (read_tree (finput, header, deref), &func);

  varpool_finalize_decl (header);
  varpool_finalize_decl (data);

  DECL_ARTIFICIAL (decl) = 1;
  DECL_RESULT (decl) =  build_decl (input_location, RESULT_DECL, NULL_TREE,
                                    integer_type_node);
  TREE_PUBLIC (decl) = 1;
  TREE_USED (decl) = 1;
  DECL_SAVED_TREE (decl) = func;
  DECL_UNINLINABLE (decl) = 1;
  TREE_STATIC (header) = 1; 
  TREE_STATIC (data) = 1; 

  DECL_INITIAL (decl) = make_node (BLOCK);
  TREE_USED (DECL_INITIAL (decl)) = 1;

  allocate_struct_function (decl, false);

  current_function_decl = decl;
  gimplify_function_tree (decl);
  cgraph_finalize_function (decl, true);

  fclose (finput);

  cgraph_finalize_compilation_unit ();
}

static void
brainfuck_langhook_write_globals (void)
{

}

tree
convert (tree type ATTRIBUTE_UNUSED, tree expr)
{
  return expr;
}

static tree
brainfuck_langhook_pushdecl (tree decl ATTRIBUTE_UNUSED)
{
  gcc_unreachable ();
}

static int
brainfuck_langhook_global_bindings_p (void)
{
  return current_function_decl == NULL ? 1 : 0;
}


struct GTY(()) lang_type
{
  char dummy;
};

struct GTY(()) lang_decl
{
  char dummy;
};


struct GTY(()) lang_identifier
{
  struct tree_identifier common;
};


union GTY((desc ("TREE_CODE (&%h.generic) == IDENTIFIER_NODE"),
	   chain_next ("(union lang_tree_node *) TREE_CHAIN (&%h.generic)")))
lang_tree_node
{
  union tree_node GTY((tag ("0"),
		       desc ("tree_node_structure (&%h)"))) generic;
  struct lang_identifier GTY((tag ("1"))) identifier;
};


struct GTY(()) language_function
{
  int dummy;
};

static tree
brainfuck_langhook_type_for_size (unsigned int bits ATTRIBUTE_UNUSED,
                                  int unsignedp ATTRIBUTE_UNUSED)
{
  return integer_type_node;
}

static tree
brainfuck_langhook_builtin_function (tree decl)
{
  return decl;
}

static tree
brainfuck_langhook_getdecls (void)
{
  return NULL;
}

static tree
brainfuck_langhook_type_for_mode (enum machine_mode mode ATTRIBUTE_UNUSED,
                                  int unsignedp ATTRIBUTE_UNUSED)
{
  enum mode_class mc = GET_MODE_CLASS (mode);
  if (mc == MODE_INT)
    return integer_type_node;
  
  return NULL_TREE;
}


void
finish_file (void)
{
}

int
brainfuck_gimplify_expr (tree *expr_p ATTRIBUTE_UNUSED,
                         gimple_seq *pre_p ATTRIBUTE_UNUSED,
                         gimple_seq *post_p ATTRIBUTE_UNUSED)
{
  return 0;
}

const char *
brainfuck_printable_name (tree decl ATTRIBUTE_UNUSED, int kind ATTRIBUTE_UNUSED)
{
  return "brainfuck";
}

struct lang_hooks lang_hooks = LANG_HOOKS_INITIALIZER;


#include "gt-brainfuck-brainfuck-lang.h"
#include "gtype-brainfuck.h"