/**
 * @file align.cpp
 * Does all the aligning stuff.
 *
 * @author  Ben Gardner
 * @license GPL v2+
 */
#include "uncrustify_types.h"
#include "chunk_list.h"
#include "ChunkStack.h"
#include "align_stack.h"
#include "prototypes.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include "unc_ctype.h"


static chunk_t *align_var_def_brace(chunk_t *pc, int span, int *nl_count);
chunk_t *align_trailing_comments(chunk_t *start);
static void align_init_brace(chunk_t *start);
static void align_func_params();
static void align_same_func_call_params();
static void align_func_proto(int span);
static void align_oc_msg_spec(int span);
static void align_typedefs(int span);
static void align_left_shift(void);
static void align_oc_msg_colon(int span);
static void align_oc_msg_string(int span);
static void add_oc_align_group(chunk_t *pc, AlignStack *st, int line_num, int start_col=0, int end_col=0, int ref_colon=0);

/*
 *   Here are the items aligned:
 *
 *   - enum value assignments
 *     enum {
 *        cat  = 1,
 *        fred = 2,
 *     };
 *
 *   - struct/union variable & bit definitions
 *     struct foo {
 *        char cat;
 *        int  id       : 5;
 *        int  name_len : 6;
 *        int  heigth   : 12;
 *     };
 *
 *   - variable definitions & assignments in normal code
 *     const char *cat = "feline";
 *     int        id   = 4;
 *     a   = 5;
 *     bat = 14;
 *
 *   - simple array initializers
 *     int a[] = {
 *        1, 2, 3, 4, 5,
 *        6, 7, 8, 9, 10
 *     };
 *
 *   - c99 array initializers
 *     const char *name[] = {
 *        [FRED]  = "fred",
 *        [JOE]   = "joe",
 *        [PETER] = "peter",
 *     };
 *     struct foo b[] = {
 *        { .id = 1,   .name = "text 1" },
 *        { .id = 567, .name = "text 2" },
 *     };
 *     struct foo_t bars[] =
 *     {
 *        [0] = { .name = "bar",
 *                .age  = 21 },
 *        [1] = { .name = "barley",
 *                .age  = 55 },
 *     };
 *
 *   - compact array initializers
 *     struct foo b[] = {
 *        { 3, "dog" },      { 6, "spider" },
 *        { 8, "elephant" }, { 3, "cat" },
 *     };
 *
 *   - multiline array initializers (2nd line indented, not aligned)
 *     struct foo b[] = {
 *        { AD_NOT_ALLOWED, "Sorry, you failed to guess the password.",
 *          "Try again?", "Yes", "No" },
 *        { AD_SW_ERROR,    "A software error has occured.", "Bye!", NULL, NULL },
 *     };
 *
 *   - Trailing comments
 *
 *   - Back-slash newline groups
 *
 *   - Function prototypes
 *     int  foo();
 *     void bar();
 *
 *   - Preprocessors
 *     #define FOO_VAL        15
 *     #define MAX_TIMEOUT    60
 *     #define FOO(x)         ((x) * 65)
 *
 *   - typedefs
 *     typedef uint8_t     BYTE;
 *     typedef int32_t     INT32;
 *     typedef uint32_t    UINT32;
 */

/**
 * Aligns everything in the chunk stack to a particular column.
 * The stack is empty after this function.
 *
 * @param col           the column
 * @param align_single  align even if there is only one item on the stack
 */
static void align_stack(ChunkStack& cs, int col, bool align_single, log_sev_t sev)
{
   chunk_t *pc;

   if (cpd.settings[UO_align_on_tabstop].b)
   {
      int rem = (col - 1) % cpd.settings[UO_output_tab_size].n;
      if (rem != 0)
      {
         LOG_FMT(sev, "%s: col=%d rem=%d", __func__, col, rem);
         col += cpd.settings[UO_output_tab_size].n - rem;
      }
   }

   if ((cs.Len() > 1) || (align_single && (cs.Len() == 1)))
   {
      LOG_FMT(sev, "%s: max_col=%d\n", __func__, col);
      while ((pc = cs.Pop()) != NULL)
      {
         if (cpd.settings[UO_align_right_cmt_at_col].n == 0)
         {
            indent_to_column(pc, col);
         }
         else
         {
            align_to_column(pc, col);
         }
         pc->flags |= PCF_WAS_ALIGNED;

         if (pc->type == CT_NL_CONT)
         {
            LOG_FMT(sev, "%s: indented [NL_CONT] on line %d to %d\n",
                    __func__, pc->orig_line, pc->column);
         }
         else
         {
            LOG_FMT(sev, "%s: indented [%.*s] on line %d to %d\n",
                    __func__, pc->len, pc->str, pc->orig_line, pc->column);
         }
      }
   }
   cs.Reset();
}


/**
 * Adds an item to the align stack and adjust the nl_count and max_col.
 * Adjust max_col as needed
 *
 * @param pc         the item to add
 * @param max_col    pointer to the column variable
 * @param extra_pad  extra padding
 */
static void align_add(ChunkStack& cs, chunk_t *pc, int& max_col, int min_pad, bool squeeze)
{
   chunk_t *prev;
   int     min_col;

   prev = chunk_get_prev(pc);
   if ((prev == NULL) || chunk_is_newline(prev))
   {
      min_col = squeeze ? 1 : pc->column;
      LOG_FMT(LALADD, "%s: pc->col=%d max_col=%d min_pad=%d min_col=%d\n",
              __func__, pc->column, max_col, min_pad, min_col);
   }
   else
   {
      if (prev->type == CT_COMMENT_MULTI)
      {
         min_col = prev->orig_col_end + min_pad;
      }
      else
      {
         min_col = prev->column + prev->len + min_pad;
      }
      if (!squeeze)
      {
         if (min_col < pc->column)
         {
            min_col = pc->column;
         }
      }
      LOG_FMT(LALADD, "%s: pc->col=%d max_col=%d min_pad=%d min_col=%d multi:%s prev->col=%d prev->len=%d %s\n",
              __func__, pc->column, max_col, min_pad, min_col, (prev->type == CT_COMMENT_MULTI) ? "Y" : "N",
              (prev->type == CT_COMMENT_MULTI) ? prev->orig_col_end : prev->column, prev->len, get_token_name(prev->type));
   }

   if (cs.Empty())
   {
      max_col = 0;
   }

   cs.Push(pc);
   if (min_col > max_col)
   {
      max_col = min_col;
   }
}


void quick_align_again(void)
{
   chunk_t    *pc;
   chunk_t    *tmp;
   AlignStack as;

   LOG_FMT(LALAGAIN, "%s:\n", __func__);
   for (pc = chunk_get_head(); pc != NULL; pc = chunk_get_next(pc))
   {
      if ((pc->align.next != NULL) && (pc->flags & PCF_ALIGN_START) && !pc->align.oc_msg_align)
      {
         as.Start(100, 0);
         as.m_right_align  = pc->align.right_align;
         as.m_oc_msg_align = pc->align.oc_msg_align;
         as.m_star_style   = (AlignStack::StarStyle)pc->align.star_style;
         as.m_amp_style    = (AlignStack::StarStyle)pc->align.amp_style;
         as.m_gap          = pc->align.gap;

         LOG_FMT(LALAGAIN, "   [%.*s:%d]", pc->len, pc->str, pc->orig_line);
         as.Add(pc->align.start);
         for (tmp = pc->align.next; tmp != NULL; tmp = tmp->align.next)
         {
            as.Add(tmp->align.start);
            LOG_FMT(LALAGAIN, " => [%.*s:%d]", tmp->len, tmp->str, tmp->orig_line);
         }
         LOG_FMT(LALAGAIN, "\n");
         as.End();
      }
   }
}


void align_all(void)
{
   if (cpd.settings[UO_align_typedef_span].n > 0)
   {
      align_typedefs(cpd.settings[UO_align_typedef_span].n);
   }

   if (cpd.settings[UO_align_left_shift].b)
   {
      align_left_shift();
   }

   if (cpd.settings[UO_align_oc_msg_colon_span].n > 0)
   {
      align_oc_msg_colon(cpd.settings[UO_align_oc_msg_colon_span].n);
   }
   
   if (cpd.settings[UO_align_oc_msg_string_literal].b)
   {
      align_oc_msg_string(cpd.settings[UO_align_oc_msg_colon_span].n);
   }
   
   /* Align variable definitions */
   if ((cpd.settings[UO_align_var_def_span].n > 0) ||
       (cpd.settings[UO_align_var_struct_span].n > 0))
   {
      align_var_def_brace(chunk_get_head(), cpd.settings[UO_align_var_def_span].n, NULL);
   }

   /* Align assignments */
   align_assign(chunk_get_head(),
                cpd.settings[UO_align_assign_span].n,
                cpd.settings[UO_align_assign_thresh].n);

   /* Align structure initializers */
   if (cpd.settings[UO_align_struct_init_span].n > 0)
   {
      align_struct_initializers();
   }

   /* Align function prototypes */
   if ((cpd.settings[UO_align_func_proto_span].n > 0) &&
       !cpd.settings[UO_align_mix_var_proto].b)
   {
      align_func_proto(cpd.settings[UO_align_func_proto_span].n);
   }

   /* Align function prototypes */
   if (cpd.settings[UO_align_oc_msg_spec_span].n > 0)
   {
      align_oc_msg_spec(cpd.settings[UO_align_oc_msg_spec_span].n);
   }

   /* Align variable defs in function prototypes */
   if (cpd.settings[UO_align_func_params].b)
   {
      align_func_params();
   }

   if (cpd.settings[UO_align_same_func_call_params].b)
   {
      align_same_func_call_params();
   }
   
   /* Just in case something was aligned out of order... do it again */
   quick_align_again();
}


/**
 * Aligns all function prototypes in the file.
 */
static void align_oc_msg_spec(int span)
{
   chunk_t    *pc;
   AlignStack as;

   LOG_FMT(LALIGN, "%s\n", __func__);
   as.Start(span, 0);

   for (pc = chunk_get_head(); pc != NULL; pc = chunk_get_next(pc))
   {
      if (chunk_is_newline(pc))
      {
         as.NewLines(pc->nl_count);
      }
      else if (pc->type == CT_OC_MSG_SPEC)
      {
         as.Add(pc);
      }
   }
   as.End();
}


/**
 * Aligns all backslash-newline combos in the file.
 * This should be done LAST.
 */
void align_backslash_newline(void)
{
   chunk_t *pc;

   pc = chunk_get_head();
   while (pc != NULL)
   {
      if (pc->type != CT_NL_CONT)
      {
         pc = chunk_get_next_type(pc, CT_NL_CONT, -1);
         continue;
      }
      pc = align_nl_cont(pc);
   }
}


void align_right_comments(void)
{
   chunk_t *pc;
   chunk_t *prev;
   bool    skip;

   for (pc = chunk_get_head(); pc != NULL; pc = chunk_get_next(pc))
   {
      if ((pc->type == CT_COMMENT) || (pc->type == CT_COMMENT_CPP))
      {
         skip = false;
         if (pc->parent_type == CT_COMMENT_END)
         {
            prev = chunk_get_prev(pc);
            if (pc->column <= (prev->orig_col_end + cpd.settings[UO_align_right_cmt_gap].n))
            {
               LOG_FMT(LALTC, "NOT changing END comment on line %d (%d <= %d + %d)\n",
                       pc->orig_line,
                       pc->column, prev->orig_col_end, cpd.settings[UO_align_right_cmt_gap].n);
               skip = true;
            }
            if (!skip)
            {
               LOG_FMT(LALTC, "Changing END comment on line %d into a RIGHT-comment\n",
                       pc->orig_line);
               pc->flags |= PCF_RIGHT_COMMENT;
            }
         }

         /* Change certain WHOLE comments into RIGHT-alignable comments */
         if (pc->parent_type == CT_COMMENT_WHOLE)
         {
            int tmp_col = 1 + (pc->brace_level * cpd.settings[UO_indent_columns].n);

            /* If the comment is further right than the brace level... */
            if (pc->column > (tmp_col + cpd.settings[UO_align_right_cmt_gap].n))
            {
               LOG_FMT(LALTC, "Changing WHOLE comment on line %d into a RIGHT-comment\n",
                       pc->orig_line);

               pc->flags |= PCF_RIGHT_COMMENT;
            }
         }
      }
   }

   pc = chunk_get_head();
   while (pc != NULL)
   {
      if ((pc->flags & PCF_RIGHT_COMMENT) != 0)
      {
         pc = align_trailing_comments(pc);
      }
      else
      {
         pc = chunk_get_next(pc);
      }
   }
}


/**
 * Aligns stuff inside a multi-line "= { ... }" sequence.
 */
void align_struct_initializers(void)
{
   chunk_t *pc;
   chunk_t *prev;

   pc = chunk_get_head();
   while (pc != NULL)
   {
      prev = chunk_get_prev_ncnl(pc);
      if ((prev != NULL) && (prev->type == CT_ASSIGN) &&
          ((pc->type == CT_BRACE_OPEN) ||
           ((cpd.lang_flags & LANG_D) && (pc->type == CT_SQUARE_OPEN))))
      {
         align_init_brace(pc);
      }
      pc = chunk_get_next_type(pc, CT_BRACE_OPEN, -1);
   }
}


/**
 * Scans the whole file for #defines. Aligns all within X lines of each other
 */
void align_preprocessor(void)
{
   chunk_t    *pc;
   AlignStack as;    // value macros
   AlignStack asf;   // function macros
   AlignStack *cur_as = &as;

   as.Start(cpd.settings[UO_align_pp_define_span].n);
   as.m_gap = cpd.settings[UO_align_pp_define_gap].n;

   asf.Start(cpd.settings[UO_align_pp_define_span].n);
   asf.m_gap = cpd.settings[UO_align_pp_define_gap].n;

   pc = chunk_get_head();
   while (pc != NULL)
   {
      /* Note: not counting back-slash newline combos */
      if (pc->type == CT_NEWLINE)
      {
         as.NewLines(pc->nl_count);
         asf.NewLines(pc->nl_count);
      }

      /* If we aren't on a 'define', then skip to the next non-comment */
      if (pc->type != CT_PP_DEFINE)
      {
         pc = chunk_get_next_nc(pc);
         continue;
      }

      /* step past the 'define' */
      pc = chunk_get_next_nc(pc);
      if (pc == NULL)
      {
         break;
      }

      LOG_FMT(LALPP, "%s: define (%.*s) on line %d col %d\n",
              __func__, pc->len, pc->str, pc->orig_line, pc->column);

      cur_as = &as;
      if (pc->type == CT_MACRO_FUNC)
      {
         cur_as = &asf;

         /* Skip to the close paren */
         pc = chunk_get_next_nc(pc); // point to open (
         pc = chunk_get_next_type(pc, CT_FPAREN_CLOSE, pc->level);

         LOG_FMT(LALPP, "%s: jumped to (%.*s) on line %d col %d\n",
                 __func__, pc->len, pc->str, pc->orig_line, pc->column);
      }

      /* step to the value past the close paren or the macro name */
      pc = chunk_get_next(pc);
      if (pc == NULL)
      {
         break;
      }

      /* don't align anything if the first line ends with a newline before
       * a value is given */
      if (!chunk_is_newline(pc))
      {
         LOG_FMT(LALPP, "%s: align on '%.*s', line %d col %d\n",
                 __func__, pc->len, pc->str, pc->orig_line, pc->column);

         cur_as->Add(pc);
      }
   }

   as.End();
   asf.End();
}


/**
 * Aligns all assignment operators on the same level as first, starting with
 * first.
 *
 * For variable definitions, only consider the '=' for the first variable.
 * Otherwise, only look at the first '=' on the line.
 */
chunk_t *align_assign(chunk_t *first, int span, int thresh)
{
   int     my_level;
   chunk_t *pc;
   int     tmp;
   int     var_def_cnt = 0;
   int     equ_count   = 0;

   if (first == NULL)
   {
      return(NULL);
   }
   my_level = first->level;

   if (span <= 0)
   {
      return(chunk_get_next(first));
   }

   LOG_FMT(LALASS, "%s[%d]: checking %.*s on line %d - span=%d thresh=%d\n",
           __func__, my_level, first->len, first->str, first->orig_line,
           span, thresh);

   AlignStack as;    // regular assigns
   AlignStack vdas;  // variable def assigns

   as.Start(span, thresh);
   as.m_right_align = true;
   vdas.Start(span, thresh);
   vdas.m_right_align = true;

   pc = first;
   while ((pc != NULL) && ((pc->level >= my_level) || (pc->level == 0)))
   {
      /* Don't check inside PAREN or SQUARE groups */
      if ((pc->type == CT_SPAREN_OPEN) ||
          (pc->type == CT_FPAREN_OPEN) ||
          (pc->type == CT_SQUARE_OPEN) ||
          (pc->type == CT_PAREN_OPEN))
      {
         tmp = pc->orig_line;
         pc  = chunk_skip_to_match(pc);
         if (pc != NULL)
         {
            as.NewLines(pc->orig_line - tmp);
            vdas.NewLines(pc->orig_line - tmp);
         }
         continue;
      }

      /* Recurse if a brace set is found */
      if ((pc->type == CT_BRACE_OPEN) ||
          (pc->type == CT_VBRACE_OPEN))
      {
         int myspan;
         int mythresh;

         tmp = pc->orig_line;

         if (pc->parent_type == CT_ENUM)
         {
            myspan   = cpd.settings[UO_align_enum_equ_span].n;
            mythresh = cpd.settings[UO_align_enum_equ_thresh].n;
         }
         else
         {
            myspan   = cpd.settings[UO_align_assign_span].n;
            mythresh = cpd.settings[UO_align_assign_thresh].n;
         }

         pc = align_assign(chunk_get_next_ncnl(pc), myspan, mythresh);
         if (pc != NULL)
         {
            /* do a rough count of the number of lines just spanned */
            as.NewLines(pc->orig_line - tmp);
            vdas.NewLines(pc->orig_line - tmp);
         }
         continue;
      }

      if (chunk_is_newline(pc))
      {
         as.NewLines(pc->nl_count);
         vdas.NewLines(pc->nl_count);

         var_def_cnt = 0;
         equ_count   = 0;
      }
      else if ((pc->flags & PCF_VAR_DEF) != 0)
      {
         var_def_cnt++;
      }
      else if (var_def_cnt > 1)
      {
         /* we hit the second variable def - don't look for assigns */
      }
      else if ((equ_count == 0) && (pc->type == CT_ASSIGN))
      {
         //fprintf(stderr, "%s:  ** %s level=%d line=%d col=%d prev=%d count=%d\n",
         //        __func__, pc->str, pc->level, pc->orig_line, pc->orig_col, prev_equ_type,
         //        equ_count);

         equ_count++;
         if (var_def_cnt != 0)
         {
            vdas.Add(pc);
         }
         else
         {
            as.Add(pc);
         }
      }

      pc = chunk_get_next(pc);
   }

   as.End();
   vdas.End();

   if (pc != NULL)
   {
      LOG_FMT(LALASS, "%s: done on %.*s on line %d\n",
              __func__, pc->len, pc->str, pc->orig_line);
   }
   else
   {
      LOG_FMT(LALASS, "%s: done on NULL\n", __func__);
   }

   return(pc);
}


/**
 * Counts how many '*' pointers are in a row, going backwards
 *
 * @param pc   Pointer to the last '*' in the series
 * @return     The count, including the current one
 */
int count_prev_ptr_type(chunk_t *pc)
{
   int count = 0;

   while ((pc != NULL) && (pc->type == CT_PTR_TYPE))
   {
      count++;
      pc = chunk_get_prev_nc(pc);
   }
   return(count);
}


static chunk_t *align_func_param(chunk_t *start)
{
   AlignStack as;
   chunk_t    *pc = start;

   as.Start(2, 0);

   as.m_star_style = (AlignStack::StarStyle)cpd.settings[UO_align_var_def_star_style].n;
   as.m_amp_style  = (AlignStack::StarStyle)cpd.settings[UO_align_var_def_amp_style].n;

   bool did_this_line = false;
   int  comma_count   = 0;
   int  chunk_count   = 0;

   while ((pc = chunk_get_next(pc)) != NULL)
   {
      chunk_count++;
      if (chunk_is_newline(pc))
      {
         did_this_line = false;
         comma_count   = 0;
         chunk_count   = 0;
      }
      else if (pc->level <= start->level)
      {
         break;
      }
      else if (!did_this_line && (pc->flags & PCF_VAR_DEF))
      {
         if (chunk_count > 1)
         {
            as.Add(pc);
         }
         did_this_line = true;
      }
      else if (comma_count > 0)
      {
         if (!chunk_is_comment(pc))
         {
            comma_count = 2;
            break;
         }
      }
      else if (pc->type == CT_COMMA)
      {
         comma_count++;
      }
   }

   if (comma_count <= 1)
   {
      as.End();
   }

   return(pc);
}


static void align_func_params()
{
   chunk_t *pc;

   pc = chunk_get_head();
   while ((pc = chunk_get_next(pc)) != NULL)
   {
      if ((pc->type != CT_FPAREN_OPEN) ||
          ((pc->parent_type != CT_FUNC_PROTO) &&
           (pc->parent_type != CT_FUNC_DEF) &&
           (pc->parent_type != CT_FUNC_CLASS) &&
           (pc->parent_type != CT_TYPEDEF)))
      {
         continue;
      }

      /* We're on a open paren of a prototype */
      pc = align_func_param(pc);
   }
}


static int align_params(chunk_t *start, chunk_t *chunks[], int num_chunks)
{
   int     count     = 0;
   chunk_t *pc       = start;
   bool    hit_comma = true;

   pc = chunk_get_next_type(start, CT_FPAREN_OPEN, start->level);

   while ((pc = chunk_get_next(pc)) != NULL)
   {
      if (chunk_is_newline(pc) ||
          (count >= num_chunks) ||
          (pc->type == CT_SEMICOLON) ||
          ((pc->type == CT_FPAREN_CLOSE) && (pc->level == start->level)))
      {
         break;
      }

      if (pc->level == (start->level + 1))
      {
         if (hit_comma)
         {
            chunks[count++] = pc;
            hit_comma       = false;
         }
         else if (pc->type == CT_COMMA)
         {
            hit_comma = true;
         }
      }
   }
   return(count);
}


static void align_same_func_call_params()
{
   chunk_t    *pc;
   chunk_t    *align_root = NULL;
   chunk_t    *align_cur  = NULL;
   int        align_len   = 0;
   chunk_t    *chunks[16];
   AlignStack as[16];
   AlignStack fcn_as;
   int        max_idx = -1;
   int        cur_as;
   int        idx;
   const char *add_str = NULL;

   fcn_as.Start(3);

   for (pc = chunk_get_head(); pc != NULL; pc = chunk_get_next(pc))
   {
      if (pc->type != CT_FUNC_CALL)
      {
         if (chunk_is_newline(pc))
         {
            for (idx = 0; idx <= max_idx; idx++)
            {
               as[idx].NewLines(pc->nl_count);
            }
            fcn_as.NewLines(pc->nl_count);
         }
         continue;
      }

      fcn_as.Add(pc);
      add_str = NULL;

      if (align_root != NULL)
      {
         if ((pc->len == align_root->len) &&
             (memcmp(pc->str, align_root->str, pc->len) == 0))
         {
            align_cur->align.next = pc;
            align_cur             = pc;
            align_len++;
            add_str = "  Add";
         }
         else
         {
            LOG_FMT(LASFCP, "  ++ Ended with %d fcns\n", align_len);

            /* Flush it all! */
            fcn_as.Flush();
            for (idx = 0; idx <= max_idx; idx++)
            {
               as[idx].Flush();
            }
            align_root = NULL;
         }
      }
      if (align_root == NULL)
      {
         align_root = pc;
         align_cur  = pc;
         align_len  = 1;
         add_str    = "Start";
      }

      if (add_str != NULL)
      {
         LOG_FMT(LASFCP, "%s '%.*s' on line %d -",
                 add_str, pc->len, pc->str, pc->orig_line);
         cur_as = align_params(pc, chunks, ARRAY_SIZE(chunks));
         LOG_FMT(LASFCP, " %d items:", cur_as);

         for (idx = 0; idx < cur_as; idx++)
         {
            LOG_FMT(LASFCP, " [%.*s]", chunks[idx]->len, chunks[idx]->str);
            if (idx > max_idx)
            {
               as[idx].Start(3);
               if (!cpd.settings[UO_align_number_left].b)
               {
                  if ((chunks[idx]->type == CT_NUMBER_FP) ||
                      (chunks[idx]->type == CT_NUMBER) ||
                      (chunks[idx]->type == CT_POS) ||
                      (chunks[idx]->type == CT_NEG))
                  {
                     as[idx].m_right_align = true;
                  }
               }
               max_idx = idx;
            }
            as[idx].Add(chunks[idx]);
         }
         LOG_FMT(LASFCP, "\n");
      }
   }
   fcn_as.End();
   for (idx = 0; idx <= max_idx; idx++)
   {
      as[idx].End();
   }
}


/**
 * Aligns all function prototypes in the file.
 */
static void align_func_proto(int span)
{
   chunk_t    *pc;
   bool       look_bro = false;
   AlignStack as;
   AlignStack as_br;

   LOG_FMT(LALIGN, "%s\n", __func__);
   as.Start(span, 0);
   as.m_gap = cpd.settings[UO_align_func_proto_gap].n;

   as_br.Start(span, 0);
   as_br.m_gap = cpd.settings[UO_align_single_line_brace_gap].n;

   for (pc = chunk_get_head(); pc != NULL; pc = chunk_get_next(pc))
   {
      if (chunk_is_newline(pc))
      {
         look_bro = false;
         as.NewLines(pc->nl_count);
         as_br.NewLines(pc->nl_count);
      }
      else if ((pc->type == CT_FUNC_PROTO) ||
               ((pc->type == CT_FUNC_DEF) &&
                cpd.settings[UO_align_single_line_func].b))
      {
         if ((pc->parent_type == CT_OPERATOR) &&
             cpd.settings[UO_align_on_operator].b)
         {
            as.Add(chunk_get_prev_ncnl(pc));
         }
         else
         {
            as.Add(pc);
         }
         look_bro = (pc->type == CT_FUNC_DEF) &&
                    cpd.settings[UO_align_single_line_brace].b;
      }
      else if (look_bro &&
               (pc->type == CT_BRACE_OPEN) &&
               (pc->flags & PCF_ONE_LINER))
      {
         as_br.Add(pc);
         look_bro = false;
      }
   }
   as.End();
   as_br.End();
}


/**
 * Scan everything at the current level until the close brace and find the
 * variable def align column.  Also aligns bit-colons, but that assumes that
 * bit-types are the same! But that should always be the case...
 */
static chunk_t *align_var_def_brace(chunk_t *start, int span, int *p_nl_count)
{
   chunk_t    *pc;
   chunk_t    *next;
   chunk_t    *prev;
   int        align_mask = PCF_IN_FCN_DEF | PCF_VAR_1ST;
   int        myspan     = span;
   int        mythresh   = 0;
   int        mygap      = 0;
   AlignStack as;    /* var/proto/def */
   AlignStack as_bc; /* bit-colon */
   AlignStack as_at; /* attribute */
   AlignStack as_br; /* one-liner brace open */
   bool       fp_active   = cpd.settings[UO_align_mix_var_proto].b;
   bool       fp_look_bro = false;


   if (start == NULL)
   {
      return(NULL);
   }

   /* Override the span, if this is a struct/union */
   if ((start->parent_type == CT_STRUCT) ||
       (start->parent_type == CT_UNION))
   {
      myspan   = cpd.settings[UO_align_var_struct_span].n;
      mythresh = cpd.settings[UO_align_var_struct_thresh].n;
      mygap    = cpd.settings[UO_align_var_struct_gap].n;
   }
   else
   {
      mythresh = cpd.settings[UO_align_var_def_thresh].n;
      mygap    = cpd.settings[UO_align_var_def_gap].n;
   }

   /* can't be any variable definitions in a "= {" block */
   prev = chunk_get_prev_ncnl(start);
   if ((prev != NULL) && (prev->type == CT_ASSIGN))
   {
      LOG_FMT(LAVDB, "%s: start=%.*s [%s] on line %d (abort due to assign)\n", __func__,
              start->len, start->str, get_token_name(start->type), start->orig_line);

      pc = chunk_get_next_type(start, CT_BRACE_CLOSE, start->level);
      return(chunk_get_next_ncnl(pc));
   }

   LOG_FMT(LAVDB, "%s: start=%.*s [%s] on line %d\n", __func__,
           start->len, start->str, get_token_name(start->type), start->orig_line);

   if (!cpd.settings[UO_align_var_def_inline].b)
   {
      align_mask |= PCF_VAR_INLINE;
   }

   /* Set up the var/proto/def aligner */
   as.Start(myspan, mythresh);
   as.m_gap        = mygap;
   as.m_star_style = (AlignStack::StarStyle)cpd.settings[UO_align_var_def_star_style].n;
   as.m_amp_style  = (AlignStack::StarStyle)cpd.settings[UO_align_var_def_amp_style].n;

   /* Set up the bit colon aligner */
   as_bc.Start(myspan, 0);
   as_bc.m_gap = cpd.settings[UO_align_var_def_colon_gap].n;

   as_at.Start(myspan, 0);

   /* Set up the brace open aligner */
   as_br.Start(myspan, mythresh);
   as_br.m_gap = cpd.settings[UO_align_single_line_brace_gap].n;

   bool did_this_line = false;
   pc = chunk_get_next(start);
   while ((pc != NULL) && ((pc->level >= start->level) || (pc->level == 0)))
   {
      if (chunk_is_comment(pc))
      {
         if (pc->nl_count > 0)
         {
            as.NewLines(pc->nl_count);
            as_bc.NewLines(pc->nl_count);
            as_at.NewLines(pc->nl_count);
            as_br.NewLines(pc->nl_count);
         }
         pc = chunk_get_next(pc);
         continue;
      }

      if (fp_active)
      {
         if ((pc->type == CT_FUNC_PROTO) ||
             ((pc->type == CT_FUNC_DEF) &&
              cpd.settings[UO_align_single_line_func].b))
         {
            LOG_FMT(LAVDB, "    add=[%.*s] line=%d col=%d level=%d\n",
                    pc->len, pc->str, pc->orig_line, pc->column, pc->level);

            as.Add(pc);
            fp_look_bro = (pc->type == CT_FUNC_DEF) &&
                          cpd.settings[UO_align_single_line_brace].b;
         }
         else if (fp_look_bro &&
                  (pc->type == CT_BRACE_OPEN) &&
                  (pc->flags & PCF_ONE_LINER))
         {
            as_br.Add(pc);
            fp_look_bro = false;
         }
      }

      /* process nested braces */
      if (pc->type == CT_BRACE_OPEN)
      {
         int sub_nl_count = 0;

         pc = align_var_def_brace(pc, span, &sub_nl_count);
         if (sub_nl_count > 0)
         {
            fp_look_bro   = false;
            did_this_line = false;
            as.NewLines(sub_nl_count);
            as_bc.NewLines(sub_nl_count);
            as_at.NewLines(sub_nl_count);
            as_br.NewLines(sub_nl_count);
            if (p_nl_count != NULL)
            {
               *p_nl_count += sub_nl_count;
            }
         }
         continue;
      }

      /* Done with this brace set? */
      if (pc->type == CT_BRACE_CLOSE)
      {
         pc = chunk_get_next(pc);
         break;
      }

      if (chunk_is_newline(pc))
      {
         fp_look_bro   = false;
         did_this_line = false;
         as.NewLines(pc->nl_count);
         as_bc.NewLines(pc->nl_count);
         as_at.NewLines(pc->nl_count);
         as_br.NewLines(pc->nl_count);
         if (p_nl_count != NULL)
         {
            *p_nl_count += pc->nl_count;
         }
      }

      /* don't align stuff inside parens/squares/angles */
      if (pc->level > pc->brace_level)
      {
         pc = chunk_get_next(pc);
         continue;
      }

      /* If this is a variable def, update the max_col */
      if ((pc->type != CT_FUNC_CLASS) &&
          ((pc->flags & align_mask) == PCF_VAR_1ST) &&
          ((pc->level == (start->level + 1)) ||
           (pc->level == 0)))
      {
         if (!did_this_line)
         {
            LOG_FMT(LAVDB, "    add=[%.*s] line=%d col=%d level=%d\n",
                    pc->len, pc->str, pc->orig_line, pc->column, pc->level);

            as.Add(pc);

            if (cpd.settings[UO_align_var_def_colon].b)
            {
               next = chunk_get_next_nc(pc);
               if (next->type == CT_BIT_COLON)
               {
                  as_bc.Add(next);
               }
            }
            if (cpd.settings[UO_align_var_def_attribute].b)
            {
               next = pc;
               while ((next = chunk_get_next_nc(next)) != NULL)
               {
                  if (next->type == CT_ATTRIBUTE)
                  {
                     as_at.Add(next);
                     break;
                  }
                  if ((next->type == CT_SEMICOLON) || chunk_is_newline(next))
                  {
                     break;
                  }
               }
            }
         }
         did_this_line = true;
      }
      if (pc->type == CT_BIT_COLON)
      {
         if (!did_this_line)
         {
            as_bc.Add(pc);
            did_this_line = true;
         }
      }
      pc = chunk_get_next(pc);
   }

   as.End();
   as_bc.End();
   as_at.End();
   as_br.End();

   return(pc);
}


/**
 * For a series of lines ending in backslash-newline, align them.
 * The series ends when a newline or multi-line C comment is encountered.
 *
 * @param start   Start point
 * @return        pointer the last item looked at (NULL/newline/comment)
 */
chunk_t *align_nl_cont(chunk_t *start)
{
   int        max_col = 0;
   chunk_t    *pc     = start;
   chunk_t    *tmp;
   ChunkStack cs;

   LOG_FMT(LALNLC, "%s: start on [%s] on line %d\n", __func__,
           get_token_name(start->type), start->orig_line);

   /* Find the max column */
   while ((pc != NULL) &&
          (pc->type != CT_NEWLINE) &&
          (pc->type != CT_COMMENT_MULTI))
   {
      if (pc->type == CT_NL_CONT)
      {
         align_add(cs, pc, max_col, 1, true);
      }
      pc = chunk_get_next(pc);
   }

   /* shift out to the next tabstop */
   max_col = align_tab_column(max_col);

   /* NL_CONT is always the last thing on a line */
   while ((tmp = cs.Pop()) != NULL)
   {
      tmp->flags |= PCF_WAS_ALIGNED;
      tmp->column = max_col;
   }

   return(pc);
}


enum CmtAlignType
{
   CAT_REGULAR,
   CAT_BRACE,
   CAT_ENDIF,
};

static CmtAlignType get_comment_align_type(chunk_t *cmt)
{
   chunk_t      *prev;
   CmtAlignType cmt_type = CAT_REGULAR;

   if (!cpd.settings[UO_align_right_cmt_mix].b &&
       ((prev = chunk_get_prev(cmt)) != NULL))
   {
      if ((prev->type == CT_PP_ENDIF) ||
          (prev->type == CT_PP_ELSE) ||
          (prev->type == CT_BRACE_CLOSE))
      {
         /* REVISIT: someone may want this configurable */
         if ((cmt->column - (prev->column + prev->len)) < 3)
         {
            cmt_type = (prev->type == CT_PP_ENDIF) ? CAT_ENDIF : CAT_BRACE;
         }
      }
   }
   return(cmt_type);
}


/**
 * For a series of lines ending in a comment, align them.
 * The series ends when more than align_right_cmt_span newlines are found.
 *
 * @param start   Start point
 * @return        pointer the last item looked at
 */
chunk_t *align_trailing_comments(chunk_t *start)
{
   int          max_col  = 0;
   chunk_t      *pc      = start;
   int          nl_count = 0;
   ChunkStack   cs;
   CmtAlignType cmt_type_start, cmt_type_cur;
   int          col;
   int          intended_col = cpd.settings[UO_align_right_cmt_at_col].n;

   cmt_type_start = get_comment_align_type(pc);

   /* Find the max column */
   while ((pc != NULL) && (nl_count < cpd.settings[UO_align_right_cmt_span].n))
   {
      if ((pc->flags & PCF_RIGHT_COMMENT) != 0)
      {
         cmt_type_cur = get_comment_align_type(pc);

         if (cmt_type_cur == cmt_type_start)
         {
            col = 1 + (pc->brace_level * cpd.settings[UO_indent_columns].n);
            LOG_FMT(LALADD, "%s: col=%d max_col=%d pc->col=%d pc->len=%d %s\n",
                    __func__, col, max_col, pc->column, pc->len, get_token_name(pc->type));
            if (pc->column < col)
            {
               pc->column = col;
            }
            if (pc->column < intended_col)
            {
               pc->column = intended_col;
            }
            align_add(cs, pc, max_col, 1, (intended_col != 0));
            nl_count = 0;
         }
      }
      if (chunk_is_newline(pc))
      {
         nl_count += pc->nl_count;
      }
      pc = chunk_get_next(pc);
   }

   align_stack(cs, max_col, (intended_col != 0), LALTC);

   return(chunk_get_next(pc));
}


/**
 * Shifts out all columns by a certain amount.
 *
 * @param idx  The index to start shifting
 * @param num  The number of columns to shift
 */
void ib_shift_out(int idx, int num)
{
   while (idx < cpd.al_cnt)
   {
      cpd.al[idx].col += num;
      idx++;
   }
}


/**
 * Scans a line for stuff to align on.
 *
 * We trigger on BRACE_OPEN, FPAREN_OPEN, ASSIGN, and COMMA.
 * We want to align the NEXT item.
 */
static chunk_t *scan_ib_line(chunk_t *start, bool first_pass)
{
   chunk_t *pc;
   chunk_t *next;
   chunk_t *prev_match = NULL;
   int     token_width;
   int     idx = 0;
   bool    last_was_comment = false;

   /* Skip past C99 "[xx] =" stuff */
   if (start->type == CT_SQUARE_OPEN)
   {
      start->parent_type = CT_TSQUARE;
      start            = chunk_get_next_type(start, CT_ASSIGN, start->level);
      start            = chunk_get_next_ncnl(start);
      cpd.al_c99_array = true;
   }
   pc = start;

   if (pc != NULL)
   {
      LOG_FMT(LSIB, "%s: start=%s col %d/%d line %d\n", __func__,
              get_token_name(pc->type), pc->column, pc->column, pc->orig_line);
   }

   while ((pc != NULL) && !chunk_is_newline(pc) &&
          (pc->level >= start->level))
   {
      //LOG_FMT(LSIB, "%s:     '%.*s'   col %d/%d line %d\n", __func__,
      //        pc->len, pc->str, pc->column, pc->orig_col, pc->orig_line);

      next = chunk_get_next(pc);
      if ((next == NULL) || chunk_is_comment(next))
      {
         /* do nothing */
      }
      else if ((pc->type == CT_ASSIGN) ||
               (pc->type == CT_BRACE_OPEN) ||
               (pc->type == CT_BRACE_CLOSE) ||
               (pc->type == CT_COMMA))
      {
         token_width = space_col_align(pc, next);

         /*TODO: need to handle missing structure defs? ie NULL vs { ... } ?? */

         /* Is this a new entry? */
         if (idx >= cpd.al_cnt)
         {
            LOG_FMT(LSIB, " - New   [%d] %.2d/%d - %10.10s\n", idx,
                    pc->column, token_width, get_token_name(pc->type));

            cpd.al[cpd.al_cnt].type = pc->type;
            cpd.al[cpd.al_cnt].col  = pc->column;
            cpd.al[cpd.al_cnt].len  = token_width;
            cpd.al_cnt++;
            idx++;
            last_was_comment = false;
         }
         else
         {
            /* expect to match stuff */
            if (cpd.al[idx].type == pc->type)
            {
               LOG_FMT(LSIB, " - Match [%d] %.2d/%d - %10.10s", idx,
                       pc->column, token_width, get_token_name(pc->type));

               /* Shift out based on column */
               if (prev_match == NULL)
               {
                  if (pc->column > cpd.al[idx].col)
                  {
                     LOG_FMT(LSIB, " [ pc->col(%d) > col(%d) ] ",
                             pc->column, cpd.al[idx].col);

                     ib_shift_out(idx, pc->column - cpd.al[idx].col);
                     cpd.al[idx].col = pc->column;
                  }
               }
               else if (idx > 0)
               {
                  int min_col_diff = pc->column - prev_match->column;
                  int cur_col_diff = cpd.al[idx].col - cpd.al[idx - 1].col;
                  if (cur_col_diff < min_col_diff)
                  {
                     LOG_FMT(LSIB, " [ min_col_diff(%d) > cur_col_diff(%d) ] ",
                             min_col_diff, cur_col_diff);
                     ib_shift_out(idx, min_col_diff - cur_col_diff);
                  }
               }
               LOG_FMT(LSIB, " - now col %d, len %d\n", cpd.al[idx].col, cpd.al[idx].len);
               idx++;
            }
         }
         prev_match = pc;
      }
      last_was_comment = chunk_is_comment(pc);
      pc = chunk_get_next_nc(pc);
   }

   //if (last_was_comment && (cpd.al[cpd.al_cnt - 1].type == CT_COMMA))
   //{
   //   cpd.al_cnt--;
   //}
   return(pc);
}


static void align_log_al(log_sev_t sev, int line)
{
   int idx;

   if (log_sev_on(sev))
   {
      log_fmt(sev, "%s: line %d, %d)", __func__, line, cpd.al_cnt);
      for (idx = 0; idx < cpd.al_cnt; idx++)
      {
         log_fmt(sev, " %d/%d=%s", cpd.al[idx].col, cpd.al[idx].len,
                 get_token_name(cpd.al[idx].type));
      }
      log_fmt(sev, "\n");
   }
}


/**
 * Generically aligns on '=', '{', '(' and item after ','
 * It scans the first line and picks up the location of those tags.
 * It then scans subsequent lines and adjusts the column.
 * Finally it does a second pass to align everything.
 *
 * Aligns all the '=' signs in stucture assignments.
 * a = {
 *    .a    = 1;
 *    .type = fast;
 * };
 *
 * And aligns on '{', numbers, strings, words.
 * colors[] = {
 *    {"red",   {255, 0,   0}}, {"blue",   {  0, 255, 0}},
 *    {"green", {  0, 0, 255}}, {"purple", {255, 255, 0}},
 * };
 *
 * For the C99 indexed array assignment, the leading []= is skipped (no aligning)
 * struct foo_t bars[] =
 * {
 *    [0] = { .name = "bar",
 *            .age  = 21 },
 *    [1] = { .name = "barley",
 *            .age  = 55 },
 * };
 *
 * NOTE: this assumes that spacing is at the minimum correct spacing (ie force)
 *       if it isn't, some extra spaces will be inserted.
 *
 * @param start   Points to the open brace chunk
 */
static void align_init_brace(chunk_t *start)
{
   int     idx;
   chunk_t *pc;
   chunk_t *next;
   chunk_t *prev;
   chunk_t *num_token = NULL;

   cpd.al_cnt       = 0;
   cpd.al_c99_array = false;

   LOG_FMT(LALBR, "%s: line %d, col %d\n", __func__, start->orig_line, start->column);

   pc = chunk_get_next_ncnl(start);
   pc = scan_ib_line(pc, true);
   if ((pc == NULL) || ((pc->type == CT_BRACE_CLOSE) &&
                        (pc->parent_type == CT_ASSIGN)))
   {
      /* single line - nothing to do */
      return;
   }

   do
   {
      pc = scan_ib_line(pc, false);

      /* debug dump the current frame */
      align_log_al(LALBR, pc->orig_line);

      while (chunk_is_newline(pc))
      {
         pc = chunk_get_next(pc);
      }
   } while ((pc != NULL) && (pc->level > start->level));

   /* debug dump the current frame */
   align_log_al(LALBR, start->orig_line);

   if (cpd.settings[UO_align_on_tabstop].b && (cpd.al_cnt >= 1) &&
       (cpd.al[0].type == CT_ASSIGN))
   {
      int rem = (cpd.al[0].col - 1) % cpd.settings[UO_output_tab_size].n;
      if (rem != 0)
      {
         LOG_FMT(LALBR, "%s: col=%d rem=%d", __func__, cpd.al[0].col, rem);
         cpd.al[0].col += cpd.settings[UO_output_tab_size].n - rem;
      }
   }

   pc  = chunk_get_next(start);
   idx = 0;
   do
   {
      if ((idx == 0) && (pc->type == CT_SQUARE_OPEN))
      {
         pc = chunk_get_next_type(pc, CT_ASSIGN, pc->level);
         pc = chunk_get_next(pc);
         if (pc != NULL)
         {
            LOG_FMT(LALBR, " -%d- skipped '[] =' to %s\n",
                    pc->orig_line, get_token_name(pc->type));
         }
         continue;
      }

      next = pc;
      if (idx < cpd.al_cnt)
      {
         LOG_FMT(LALBR, " (%d) check %s vs %s -- ",
                 idx, get_token_name(pc->type), get_token_name(cpd.al[idx].type));
         if (pc->type == cpd.al[idx].type)
         {
            if ((idx == 0) && cpd.al_c99_array)
            {
               prev = chunk_get_prev(pc);
               if (chunk_is_newline(prev))
               {
                  pc->flags |= PCF_DONT_INDENT;
               }
            }
            LOG_FMT(LALBR, " [%.*s] to col %d\n", pc->len, pc->str, cpd.al[idx].col);

            if (num_token != NULL)
            {
               int col_diff = pc->column - num_token->column;

               reindent_line(num_token, cpd.al[idx].col - col_diff);
               //LOG_FMT(LSYS, "-= %d =- NUM indent [%.*s] col=%d diff=%d\n",
               //        num_token->orig_line,
               //        num_token->len, num_token->str, cpd.al[idx - 1].col, col_diff);

               num_token->flags |= PCF_WAS_ALIGNED;
               num_token         = NULL;
            }

            /* Comma's need to 'fall back' to the previous token */
            if (pc->type == CT_COMMA)
            {
               next = chunk_get_next(pc);
               if ((next != NULL) && !chunk_is_newline(next))
               {
                  //LOG_FMT(LSYS, "-= %d =- indent [%.*s] col=%d len=%d\n",
                  //        next->orig_line,
                  //        next->len, next->str, cpd.al[idx].col, cpd.al[idx].len);

                  if ((idx < (cpd.al_cnt - 1)) &&
                      cpd.settings[UO_align_number_left].b &&
                      ((next->type == CT_NUMBER_FP) ||
                       (next->type == CT_NUMBER) ||
                       (next->type == CT_POS) ||
                       (next->type == CT_NEG)))
                  {
                     /* Need to wait until the next match to indent numbers */
                     num_token = next;
                  }
                  else
                  {
                     reindent_line(next, cpd.al[idx].col + cpd.al[idx].len);
                     next->flags |= PCF_WAS_ALIGNED;
                  }
               }
            }
            else
            {
               /* first item on the line */
               reindent_line(pc, cpd.al[idx].col);
               pc->flags |= PCF_WAS_ALIGNED;

               /* see if we need to right-align a number */
               if ((idx < (cpd.al_cnt - 1)) &&
                   cpd.settings[UO_align_number_left].b)
               {
                  next = chunk_get_next(pc);
                  if ((next != NULL) && !chunk_is_newline(next) &&
                      ((next->type == CT_NUMBER_FP) ||
                       (next->type == CT_NUMBER) ||
                       (next->type == CT_POS) ||
                       (next->type == CT_NEG)))
                  {
                     /* Need to wait until the next match to indent numbers */
                     num_token = next;
                  }
               }
            }
            idx++;
         }
         else
         {
            LOG_FMT(LALBR, " no match\n");
         }
      }
      if (chunk_is_newline(pc) || chunk_is_newline(next))
      {
         idx = 0;
      }
      pc = chunk_get_next(pc);
   } while ((pc != NULL) && (pc->level > start->level));
}


/**
 * Aligns simple typedefs that are contained on a single line each.
 * This should be called after the typedef target is marked as a type.
 *
 * Won't align function typedefs.
 *
 * typedef int        foo_t;
 * typedef char       bar_t;
 * typedef const char cc_t;
 */
static void align_typedefs(int span)
{
   chunk_t    *pc;
   chunk_t    *c_type    = NULL;
   chunk_t    *c_typedef = NULL;
   AlignStack as;

   as.Start(span);
   as.m_gap        = cpd.settings[UO_align_typedef_gap].n;
   as.m_star_style = (AlignStack::StarStyle)cpd.settings[UO_align_typedef_star_style].n;
   as.m_amp_style  = (AlignStack::StarStyle)cpd.settings[UO_align_typedef_amp_style].n;

   pc = chunk_get_head();
   while (pc != NULL)
   {
      if (chunk_is_newline(pc))
      {
         as.NewLines(pc->nl_count);
         c_typedef = NULL;
      }
      else if (c_typedef != NULL)
      {
         if (pc->flags & PCF_ANCHOR)
         {
            as.Add(pc);
            c_typedef = NULL;
         }
      }
      else
      {
         if (pc->type == CT_TYPEDEF)
         {
            LOG_FMT(LALTD, "%s: line %d, col %d\n",
                    __func__, pc->orig_line, pc->column);
            c_typedef = pc;
            c_type    = NULL;
         }
      }

      pc = chunk_get_next(pc);
   }

   as.End();
}


/**
 * Align '<<' (CT_ARITH?)
 */
static void align_left_shift(void)
{
   chunk_t    *pc;
   chunk_t    *start = NULL;
   AlignStack as;
   bool       skip_stmt = false;

   as.Start(2);

   pc = chunk_get_head();
   while (pc != NULL)
   {
      if (chunk_is_newline(pc))
      {
         as.NewLines(pc->nl_count);
         skip_stmt = as.m_aligned.Empty();
      }
      else if ((start != NULL) && (pc->level < start->level))
      {
         /* A drop in level restarts the aligning */
         as.Flush();
         start = NULL;
      }
      else if ((start != NULL) && (pc->level > start->level))
      {
         /* Ignore any deeper levels when aligning */
      }
      else if (pc->flags & PCF_STMT_START)
      {
         as.Reset();
         skip_stmt = false;
         start     = NULL;
      }
      else if (pc->type == CT_SEMICOLON)
      {
         as.Flush();
         start = NULL;
      }
      else if (!skip_stmt && chunk_is_str(pc, "<<", 2))
      {
         if (as.m_aligned.Empty())
         {
            as.Add(pc);
            start = pc;
         }
         else if (chunk_is_newline(chunk_get_prev(pc)))
         {
            as.Add(pc);
         }
      }

      pc = chunk_get_next(pc);
   }

   if (skip_stmt)
   {
      as.Reset();
   }
   else
   {
      as.End();
   }
}

/**
 * Check if there are any CT_OC_COLONs from pc to end of line.
 *
 * @param prev_result   a bool pointer which is needed to save the previous result
 *                      so that it is possible to have the very last line of a multi
 *                      line OC_MSG return true even though there is no following 
 *                      line with a OC_COLON.
 *
 * @param level         the level of the opening square bracket
 *
 */
bool line_has_oc_msg_colon(chunk_t *pc, int level)
{
   chunk_t * tmp = pc;
   bool result = false;
   
   /* backtrack to previous newline */
   if (pc->type == CT_NEWLINE)
   {
      int line = pc->orig_line;
      pc = chunk_get_prev_nl(pc, CNAV_PREPROC);
      if (pc == NULL) 
         return false;
      
      if (pc->orig_line < (line - 1))
      {
         pc = chunk_get_next_ncnl(pc, CNAV_PREPROC);
         pc = chunk_get_next_nc(pc, CNAV_PREPROC);
         if (!pc)
            return false;
         
      }
      if (pc->level <= level)
      {
         /* skip to open square */
         pc = chunk_get_next_type(pc, CT_SQUARE_OPEN, level, CNAV_PREPROC);
         pc = chunk_get_next_nc(pc, CNAV_PREPROC);
         if (!pc)
            return false;
      }
   }
   
   if ((pc->type == CT_COMMENT) && 
       (pc->parent_type == CT_COMMENT_WHOLE))
   {
      return false;
   }
    
   while (((tmp = chunk_get_next(tmp, CNAV_PREPROC)) != NULL) 
          && (pc->level > level) 
          && (tmp->parent_type == CT_OC_MSG))
   {
      if ((tmp->type == CT_NEWLINE) || (tmp->type == CT_SEMICOLON)) 
      {
         break;
      }
      if ((tmp->type == CT_OC_COLON) && (tmp->parent_type == CT_OC_MSG))
      {
         result = true;
         break;
      }
   }
   return result;
}


bool next_line_has_oc_msg_colon(chunk_t *pc, int level)
{
   chunk_t * tmp = pc;
   
   /* if we are already on a newline just get the next chunk */
   if (tmp->type == CT_NEWLINE)
   {
      tmp = chunk_get_next_nc(tmp, CNAV_PREPROC);
      if (!tmp) 
         return false;
   }
   else
   {      
      /* get to next line's start... */   
      while ((tmp = chunk_get_next_nc(tmp, CNAV_PREPROC)) != NULL)
      {
         if (tmp->type == CT_NEWLINE)
         {
            tmp = chunk_get_next(tmp, CNAV_PREPROC);
            break;
         }
         if ((tmp->type == CT_SQUARE_CLOSE) && (tmp->level == level)) 
         {
            /* fail-out if the msg send ends without a next line */
            return false;
         }
      }
   }
   if (!tmp)
      return false;
   
   return line_has_oc_msg_colon(tmp, level);
}


bool prev_line_has_oc_msg_colon(chunk_t *pc, int level)
{
   chunk_t * tmp = pc;
   
   /* need two newlines, first to get this line's start then to get to prev line's start... */   
   while ((tmp = chunk_get_prev_nc(tmp, CNAV_PREPROC)) != NULL)
   {
      if (tmp->type == CT_NEWLINE)
      {
            break;
      }
      else if ((tmp->type == CT_SQUARE_OPEN) && (tmp->level == level))
      { 
         /* ...but only if this is still the same msg send */
         tmp = chunk_get_next_nc(tmp, CNAV_PREPROC);
         break;
      }
   }
   
   if (!tmp)
      return false;
   
   return line_has_oc_msg_colon(tmp, level);
}

/**
 * Check if how many CT_OC_COLONs there are on pc's current line.
 * This function backtracks to the beginning of the msg send if on the same line
 * or a the last newline before it starts counting.
 *
 * This has a few caveats: 
 *
 * Do we care about every colon (even those of nested msg sends) or just the 
 * colons of a particular msg send? 
 *
 * If pc is a newline do we care about colons on the next line or the line
 * ending in this newline? Currently it seems more logical to skip to the next
 * line, but the behaviour can be set by passing true for the skip param.
 *
 * @param level         the level of the opening square bracket or -1 for 
 *                      all levels.
 *
 * @param skip          if pc is a newline, skip to the next line  
 *                      or rewind to chunk after previous newline? (default: true)
 *
 * @return              amount of colons or -1 on error (e.g. tmp becomes NULL etc.).
 */
int oc_msg_colons_for_line(chunk_t *pc, int level=-1, bool skip=true)
{
   int target_level  = level; 
   int result        = 0;
   chunk_t * tmp     = pc;
   
   if ((tmp->type == CT_NEWLINE) && skip)
   {
      tmp = chunk_get_next(tmp, CNAV_PREPROC);
   }
   else
   {     
      /* backtrack to either chunk after last newline or
         chunk after square open which starts the msg send */
      while (((tmp = chunk_get_prev(tmp, CNAV_PREPROC)) != NULL) && (tmp->level >= level))
      {
         if ((tmp->type == CT_NEWLINE) || 
             (tmp->type == CT_SQUARE_OPEN) || 
             (tmp->parent_type != CT_OC_MSG)) 
         {
            break;
         }
      }
   }
   
   if (!tmp) 
      return -1;
   
   while (((tmp = chunk_get_next_nc(tmp, CNAV_PREPROC)) != NULL) && 
          (tmp->parent_type == CT_OC_MSG) && 
          ((tmp->level >= target_level) || level == -1))
   {
      if ((tmp->type == CT_NEWLINE) || 
          (tmp->type == CT_SEMICOLON) ||
          ((tmp->type == CT_SQUARE_CLOSE) && 
           (tmp->level == target_level))) 
      {
         break;
      }
      if ((tmp->type == CT_OC_COLON)  && (tmp->level == level))
      {
         result += 1;
      }
      if (tmp->type == CT_SQUARE_OPEN)
      {
         /* skip over nested msg sends */
         tmp = chunk_get_next_type(tmp, CT_SQUARE_CLOSE, tmp->level, CNAV_PREPROC);
         if (!tmp) 
            break;
      }
   }
   
   LOG_FMT(LOCCOLON, "%s [%.*s%.*s] line %d has %d oc msg colon%s\n", "oc colons", 
           pc->prev->len, pc->prev->str, pc->len, pc->str, pc->orig_line, result, (result > 1 ? "s" : ""));
   
   return result;
}


static void add_oc_align_group(chunk_t *pc, AlignStack *st, int line_num, int start_col, int end_col, int ref_colon)
{
   if (!pc)
      return;
   
   if (line_num == 1) 
   {
      // add the colon
      (*st).Add(pc, 0, line_num, start_col, end_col, ref_colon);
   }
   else 
   {
      // otherwise add the word/type
      chunk_t *tmp = chunk_get_prev(pc);
      if ((tmp != NULL) && ((tmp->type == CT_WORD) || (tmp->type == CT_TYPE)))
      {
         (*st).Add(tmp, 0, line_num, start_col, end_col, ref_colon);
      }
   }
}


/**
 * Aligns multi-line ObjC msg sends via their selector colons.
 * It already handles a fair amount of edge cases but there is one caveat:
 * Mixed spaces and tabs make it a very unreliable feature, so make sure
 * that your document is clean either tabs or spaces.
 *
 * TODO: Nested message sends spanning multiple lines. 
 *       For each level bump we need to shift the level's reference colon
 *       over to the right for what is set as out_tab_size.
 *
 *       Currently, if we have this wrongly aligned msg send:
 *       
 *               [[NSAlert alertWithMessageText:messageText
 *                         defaultButton:@"OK"
 *                         alternateButton:@"Cancel"
 *                         otherButton:nil
 *                         informativeTextWithFormat:infoText] 
 *                         beginSheetModalForWindow:[[historyController view] window] 
 *                         modalDelegate:self 
 *                         didEndSelector:nil 
 *                         contextInfo:nil];
 *
 *       the algorithm will align all colons to the reference colon from the
 *       first line, e.g. we get this:
 *
 *               [[NSAlert alertWithMessageText:messageText
 *                                defaultButton:@"OK"
 *                              alternateButton:@"Cancel"
 *                                  otherButton:nil
 *                    informativeTextWithFormat:infoText] 
 *                     beginSheetModalForWindow:[[historyController view] window] 
 *                                modalDelegate:self 
 *                               didEndSelector:nil 
 *                                  contextInfo:nil];
 *
 *        while we should get this instead (note the shift of the last few params):
 *
 *               [[NSAlert alertWithMessageText:messageText
 *                                defaultButton:@"OK"
 *                              alternateButton:@"Cancel"
 *                                  otherButton:nil
 *                    informativeTextWithFormat:infoText] 
 *                         beginSheetModalForWindow:[[historyController view] window] 
 *                                    modalDelegate:self 
 *                                   didEndSelector:nil 
 *                                      contextInfo:nil];    
 * 
 */
static void align_oc_msg_colon(int span)
{
   chunk_t    *pc = chunk_get_head();
   chunk_t    *tmp;
   chunk_t    *colon;            /* the colon to align other chunks to. note: colon really must be a
                                    colon while all other AlignStack entries following that are words 
                                    or types. */
   
   AlignStack cas;               /* colon align stack */
   AlignStack sas;               /* string align stack */
   
   int        level;
   int        lcnt;              /* line count with no colon for span. becomes fail-out condition if >= span */
   int        lnum;              /* the line number of a multiline msg send */
   bool       has_colon;
   bool       first_line;        /* is this the first line being aligned? */
   int        colon_col;         /* column of the alignee (last or first colon of a line dep. on settings) */
   int        ref_col;           /* the column of the reference colon */
   int        line_start_col;    /* column of the start of a msg send line */
   int        line_end_col;      /* column of the end of a msg send line */
   int        last_skipped_lnum; /* line no of the last skipped line */

   while (pc != NULL)
   {
      if ((pc->type != CT_SQUARE_OPEN) || (pc->parent_type != CT_OC_MSG))
      {
         pc = chunk_get_next(pc);
         continue;
      }
               
      cas.Start(span);
      
      cas.m_oc_msg_align = true;
      
      level = pc->level;
      pc    = chunk_get_next_ncnl(pc, CNAV_PREPROC);
      
      lnum              = 1;  
      lcnt              = 0;
      ref_col           = 0;
      colon_col         = 0;
      line_end_col      = 0;  
      line_start_col    = 0;
      last_skipped_lnum = 0;
      
      colon          = NULL;
      
      has_colon      = false;
      first_line     = true;

      bool has_multiple_colons = false;
      bool align_on_first      = (cpd.settings[UO_align_oc_msg_on_first_colon].b);
      
      while (pc != NULL)
      {
         /* break on closing square */
         if (pc->level == level)
            break;            
         
         if (pc->parent_type == CT_COMMENT_WHOLE)
         {
            ++lcnt;
            if (lcnt >= span)
               break;
         }
         else if (chunk_is_newline(pc))
         {
            if (colon != NULL) 
            {
               colon_col      = colon->orig_col;
               line_end_col   = pc->orig_col;
               
               /* figure out col of first statement chunk of line */
               tmp = chunk_get_prev_nl(colon, CNAV_PREPROC);
               if (tmp != NULL)
               {
                  tmp = chunk_get_next_nc(tmp, CNAV_PREPROC);
                  if (tmp != NULL) 
                  {
                     line_start_col = tmp->orig_col;
                  }
               }
               
               add_oc_align_group(colon, &cas, lnum, line_start_col, line_end_col, colon_col);
               colon = NULL;
            }
            
            if (!has_colon)
            {
               ++lcnt;
            }
            else
            {
               lcnt = 0;
            }
            
            ++lnum;
            
            first_line          = false;
            has_colon           = false;
            has_multiple_colons = false;
            
            if (pc->level <= level)
            {
               break;               
            }
         }
         else if (pc->type == CT_OC_COLON && 
                  (next_line_has_oc_msg_colon(pc, level) ||
                   prev_line_has_oc_msg_colon(pc, level)))
         {

            has_colon = true;
            
            /* on the first line we just need to find out the colon for alignment */
            if (first_line)
            {               
               /* align to first */
               if (align_on_first)
               {
                  colon = pc;
                  ref_col = colon->orig_col;
                  
                  /* skip remaining colons on the same line and 
                     continue with next line or stop on closing square */
                  while ((pc = chunk_get_next_nc(pc, CNAV_PREPROC)) != NULL) 
                  {
                     if (((pc->type == CT_SQUARE_CLOSE) && (pc->level == level)) || pc->type == CT_NEWLINE)
                     {
                        pc = chunk_get_prev_nc(pc, CNAV_PREPROC);
                        break;
                     }
                  }
               }
               else /* align on last */
               {
                  colon = chunk_get_next_nl(pc, CNAV_PREPROC);
                  colon = chunk_get_prev_type(colon, CT_OC_COLON, -1);
                  
                  if (colon == NULL) 
                  {
                     break;
                  }
                  
                  ref_col = colon->orig_col;
                  pc = colon;
               }
            }
            else if ((lcnt < span) && (lnum != last_skipped_lnum))
            {
               
               if (((ref_col == pc->column) ||      /* no need to align already aligned chunks */
                    (ref_col == pc->orig_col)))     /* or previously aligned chunks */
               {
                  last_skipped_lnum = lnum;
                  pc = chunk_get_next(pc, CNAV_PREPROC);
                  continue;
               }
               
               if (align_on_first)
               {                  
                  /* figure out end col of last statement of line */
                  tmp = chunk_get_next_nl(pc, CNAV_PREPROC);
                  if (tmp != NULL)
                  {
                     line_end_col = tmp->column;
                  }
                  
                  /* figure out col of first statement of line */
                  tmp = chunk_get_prev_nl(pc, CNAV_PREPROC);
                  if (tmp != NULL)
                  {
                     tmp = chunk_get_next_nc(tmp, CNAV_PREPROC);
                     if (tmp != NULL) 
                     {
                        line_start_col = tmp->orig_col;
                     }
                  }

                  add_oc_align_group(pc, &cas, lnum, line_start_col, line_end_col, pc->column);
                  
                  while ((pc = chunk_get_next_nc(pc, CNAV_PREPROC)) != NULL) 
                  {
                     if (((pc->type == CT_SQUARE_CLOSE) && (pc->level == level)) || pc->type == CT_NEWLINE)
                     {
                        pc = chunk_get_prev_nc(pc, CNAV_PREPROC);
                        break;
                     }
                  }
               }
               else /* align on last */
               {
                  /* lines of a msg send containing multiple 
                     colons need to be treated differently 
                     depending on the settings */
                  if (has_multiple_colons || (oc_msg_colons_for_line(pc, pc->level) > 1)) 
                  {
                     /* cache to avoid oc_msg_colons_for_line */
                     has_multiple_colons = true;
                     
                     /* figure out col of first statement of line */
                     tmp = chunk_get_prev_nl(pc, CNAV_PREPROC);
                     if (tmp != NULL)
                     {
                        tmp = chunk_get_next_nc(tmp, CNAV_PREPROC);
                        if (tmp != NULL) 
                        {
                           line_start_col = tmp->orig_col;
                        }
                     }
                     
                     /* figure out end col of last statement of line */
                     tmp = chunk_get_next_nl(pc, CNAV_PREPROC);
                     if (tmp != NULL)
                     {
                        line_end_col = tmp->column;
                     }
                     
                     /* figure out column of the adjusted colon */
                     tmp = chunk_get_prev_type(tmp, CT_OC_COLON, -1);
                     if (tmp != NULL) 
                     {
                        colon_col = tmp->column;
                     }
                     
                     add_oc_align_group(pc, &cas, lnum, line_start_col, line_end_col, colon_col);
                     
                     pc = tmp;
                  }
                  else /* single colon */
                  {                     
                     /* figure out col of first statement of line */
                     tmp = chunk_get_prev_nl(pc, CNAV_PREPROC);
                     if (tmp != NULL)
                     {
                        tmp = chunk_get_next_nc(tmp, CNAV_PREPROC);
                        if (tmp != NULL) 
                        {
                           line_start_col = tmp->orig_col;
                        }
                     }
                     
                     /* figure out end col of last statement of line */
                     tmp = chunk_get_next_nl(pc, CNAV_PREPROC);
                     if (tmp != NULL)
                     {
                        line_end_col = tmp->column;
                     }
                     
                     colon_col    = pc->column;
                     
                     add_oc_align_group(pc, &cas, lnum, line_start_col, line_end_col, colon_col);
                     
                  } // if (has_multiple_colons...)
                  
               } // if (align_on_first)
               
            } // if (first_line)
            
         } // if (pc->parent_type == CT_COMMENT_WHOLE)
         
//          if ((align_str_literals) && 
//                   (pc->type == CT_STRING) && 
//                   (pc->parent_type == CT_OC_MSG))
//          {
//             /* align consecutive string literals */
//             
//             int first_str_col = 0;
//             int last_str_line = pc->orig_line;
//             
//             /* captures if we have seen the pattern ... :@"string" on a line before
//                the contestant. if we don't bail out. this is because probably
//                ... :@"string1"\n@"string2" ... should be treated differently than
//                ... :\n@"string1"\n@"string2" ... Ideally this shouldn't matter at all
//                and we should just be able to always align other strings to the first
//                multi-line spaced string but that would have to be iterated on a per
//                method call basis and how do you know that you are in a new method call?
//                The levels won't help here. We would need to parse the whole document and
//                make a cheap lookup table of each method call a bit like its used for 
//                dynamic dispatch and that is just overkill for this. */
//             bool prev_result  = false;    
//                                         
//             chunk_t * tmp2 = NULL;
//             chunk_t * str  = pc;
//             
//             if (str != NULL)
//             {
//                if (((tmp2 = chunk_get_prev(str, CNAV_PREPROC)) != NULL) && (tmp2->type == CT_OC_COLON))
//                {
//                   prev_result = true;
//                }
//                
//                if (first_str_col == 0)
//                {
//                   first_str_col = str->column;
//                }
//                
//                while (((str = chunk_get_next_type(str, CT_STRING, str->level, CNAV_PREPROC)) != NULL) && prev_result)
//                {                  
//                   /* if we're on the next line and its prev is a newline,
//                      e.g. CT_STRING is the only type on this line... */
//                   if ((str->orig_line == (last_str_line + 1)) && 
//                       (str->prev != NULL) && 
//                       (str->prev->type == CT_NEWLINE))
//                   {
//                      last_str_line  = str->orig_line;
//                      line_start_col = str->column;
//                      
//                      tmp2 = chunk_get_next_nl(pc, CNAV_PREPROC);
//                      if (tmp2 != NULL)
//                      {
//                         line_end_col = tmp2->column;
//                      }
//                      
//                      sas.Add(pc, 0, 0, line_start_col, line_end_col, first_str_col);
//                      sas.Add(str, 0, 0, line_start_col, line_end_col, first_str_col);
//                      
//                      pc = str;
//                   }
//                   else
//                   {
//                      first_str_col = 0;
//                      prev_result = false;
//                   }
//                }               
//             }
//          }
          
         pc = chunk_get_next(pc, CNAV_PREPROC);
      }
      
      cas.End();
   }
}


/**
 * Works pretty much like align_oc_msg_colon() in that it gives 
 * the file the once-over by aligning ObjC string literals 
 * (@"...") used as parameters in ObjC msg sends.
 * 
 * e.g. if we have a msg send like:
 * 
 * [obj msgWithArg:arg
 *             anotheArg:arg2
 *           andFormat:@"some format string",
 *                 @"content text",
 *                  @"still more content text"];
 *
 * this would become:
 *
 * [obj msgWithArg:arg
 *       anotheArg:arg2
 *       andFormat:@"some format string",
 *                 @"content text",
 *                 @"still more content text"];
 *
 * If, however, we have a msg send that inserts a newline right 
 * after the first colon it is likely that the following string 
 * literal params have been aligned manually and should be left
 * as is.
 *
 * e.g. don't modify
 *
 * [obj msgWithFormat:
 *             @"some format string",
 *             @"content text",
 *             @"still more content text"];
 *
 * Note that following the order of execution in allign_all(), ObjC
 * msg send colons are aligned first, then the string literal params.
 * Also note that span is not really used here currently.
 */
static void align_oc_msg_string(int span)
{
   chunk_t    *pc = chunk_get_head();
   chunk_t    *tmp;
   chunk_t    *cstr;
   
   AlignStack sas;               /* string align stack */
   
   int        level;
   int        lcnt;              /* line count with no colon for span. becomes fail-out condition if >= span */
   int        lnum;              /* the line number of a multiline msg send */
   int        line_start_col;    /* column of the start of a msg send line */
   int        line_end_col;      /* column of the end of a msg send line */
   int        last_skipped_lnum; /* line no of the last skipped line */
   
   while (pc != NULL)
   {
      if ((pc->type != CT_SQUARE_OPEN) || (pc->parent_type != CT_OC_MSG))
      {
         pc = chunk_get_next(pc);
         continue;
      }
      
      sas.Start(span);      
      sas.m_oc_str_align = true;
      
      level = pc->level;
      pc    = chunk_get_next_ncnl(pc, CNAV_PREPROC);
      
      lnum              = 1;  
      lcnt              = 0;
      line_end_col      = 0;  
      line_start_col    = 0;
      last_skipped_lnum = 0;
      
      
      while (pc != NULL)
      {
         /* break on closing square */
         if (pc->level == level)
            break;            
         
         if (pc->parent_type == CT_COMMENT_WHOLE)
         {
            ++lcnt;
            if (lcnt >= span)
               break;
         }

         if ((pc->type == CT_STRING) && 
             (pc->parent_type == CT_OC_MSG))
         {            
            int first_str_col = 0;
            int last_str_line = pc->orig_line;
            
            /* captures if we have seen the pattern ... :@"string" on a line before
               the contestant. if we don't bail out. this is because probably
               ... :@"string1"\n@"string2" ... should be treated differently than
               ... :\n@"string1"\n@"string2" ... Ideally this shouldn't matter at all
               and we should just be able to always align other strings to the first
               multi-line spaced string but that would have to be iterated on a per
               method call basis and how do you know that you are in a new method call?
               The levels won't help here. We would need to parse the whole document and
               make a cheap lookup table of each method call a bit like its used for 
               dynamic dispatch and that is just overkill for this. */
            bool has_colon_string_pattern = false;    
            
            tmp = NULL;
            cstr  = pc;
            
            if (cstr != NULL)
            {
               if (((tmp = chunk_get_prev(cstr, CNAV_PREPROC)) != NULL) && (tmp->type == CT_OC_COLON))
               {
                  has_colon_string_pattern = true;
               }
               
               if (first_str_col == 0)
               {
                  first_str_col = cstr->column;
               }
               
               while (((cstr = chunk_get_next_type(cstr, CT_STRING, cstr->level, CNAV_PREPROC)) != NULL) && (has_colon_string_pattern))
               {                  
                  /* if we're on the next line and its prev is a newline,
                   e.g. CT_STRING is the only type on this line... */
                  if ((cstr->orig_line == (last_str_line + 1)) && 
                      (cstr->prev != NULL) && 
                      (cstr->prev->type == CT_NEWLINE))
                  {
                     last_str_line  = cstr->orig_line;
                     line_start_col = cstr->column;
                     
                     tmp = chunk_get_next_nl(pc, CNAV_PREPROC);
                     if (tmp != NULL)
                     {
                        line_end_col = tmp->column;
                     }
                     
                     sas.Add(pc, 0, 0, line_start_col, line_end_col, first_str_col);
                     sas.Add(cstr, 0, 0, line_start_col, line_end_col, first_str_col);
                     
                     pc = cstr;
                  }
                  else
                  {
                     first_str_col = 0;
                     has_colon_string_pattern = false;
                  }
               }               
            }
         }
         
         pc = chunk_get_next(pc, CNAV_PREPROC);
      }
      
      sas.End();
   }   
}
