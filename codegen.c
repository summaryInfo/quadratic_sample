#include "expr-impl.h"

#include <stdio.h>
#include <string.h>

static void do_codegen(struct expr *exp, FILE *out) {
    // Well all this code generation is *very* inefficient
    // due to the lack of proper types (doubles are used for conditionals)
    // and the fact that the only comparison operations for doubles are < and >
    static size_t label_n = 0;
    assert(exp && exp->tag <= t_MAX);

    enum tag tag = exp->tag;

    switch(tag) {
    case t_constant:
        fprintf(out, "\tld.d $%lf\n", exp->value);
        break;
    case t_variable:
        fprintf(out, "\tld.d %s\n", exp->id);
        break;
    case t_power:
        do_codegen(exp->children[0], out);
        do_codegen(exp->children[1], out);
        fputs("\tcall.d power_d\n", out);
        break;
    case t_log:
        do_codegen(exp->children[0], out);
        fputs("\tcall.d log_d\n", out);
        break;
    case t_negate:
        do_codegen(exp->children[0], out);
        fputs("\tneg.d\n", out);
        break;
    case t_assign:
        do_codegen(exp->children[1], out);
        assert(exp->children[0]->tag == t_variable);
        fprintf(out, "\tdup.l\n"
                     "\tst.d %s\n", exp->children[0]->id);
        break;
    case t_inverse:
        assert(0);
        break;
    case t_add:
        do_codegen(exp->children[0], out);
        for (size_t i = 1; i < exp->n_child; i++) {
            if (exp->children[i]->tag == t_negate) {
                do_codegen(exp->children[i]->children[0], out);
                fputs("\tsub.d\n", out);
            } else {
                do_codegen(exp->children[i], out);
                fputs("\tadd.d\n", out);
            }
        }
        break;
    case t_multiply:
        do_codegen(exp->children[0], out);
        for (size_t i = 1; i < exp->n_child; i++) {
            if (exp->children[i]->tag == t_inverse) {
                do_codegen(exp->children[i]->children[0], out);
                fputs("\tdiv.d\n", out);
            } else {
                do_codegen(exp->children[i], out);
                fputs("\tmul.d\n", out);
            }
        }
        break;
    case t_less:
    case t_greater:
    case t_lessequal:
    case t_greaterequal:
    case t_equal:
    case t_notequal:
    case t_logical_not: {
        size_t l1 = label_n++, l2 = label_n++;
        do_codegen(exp->children[0], out);
        if (tag != t_logical_not) do_codegen(exp->children[1], out);
        switch (tag) {
        case t_less:
            fprintf(out, "\tjl.d L%zu\n", l1);
            break;
        case t_greater:
            fprintf(out, "\tjg.d L%zu\n", l1);
            break;
        case t_lessequal:
            fprintf(out, "\tld.d $%lf\n"
                         "\tadd.d\n"
                         "\tjl.d L%zu\n", EPS, l1);
            break;
        case t_greaterequal:
            fprintf(out, "\tld.d $%lf\n"
                         "\tsub.d\n"
                         "\tjg.d L%zu\n", EPS, l1);
            break;
        case t_equal:
            fprintf(out, "\tsub.d\n");
            //fallthrough
        case t_logical_not:
            fprintf(out, "\tcall.d abs_d\n"
                         "\tld.d $%lf\n"
                         "\tjl.d L%zu\n", EPS, l1);
            break;
        case t_notequal:
            fprintf(out, "\tsub.d\n"
                         "\tcall.d abs_d\n"
                         "\tld.d $%lf\n"
                         "\tjg.d L%zu\n", EPS, l1);
            break;
        default:
            assert(0);
        }
        fprintf(out, "\tld.d $0\n"
                     "\tjmp L%zu\n"
                     "L%zu:\n"
                     "\tld.d $1\n"
                     "L%zu:\n", l2, l1, l2);
        break;
    }
    case t_logical_and:
    case t_logical_or: {
        size_t lend = label_n++, lend2 = label_n++;
        do_codegen(exp->children[0], out);
        for (size_t i = 1; i < exp->n_child; i++) {
            fprintf(out, "\tcall.d abs_d\n"
                         "\tld.d $%lf\n"
                         "\tj%c.d L%zu\n", EPS, tag == t_logical_or ? 'g' : 'l', lend);
            do_codegen(exp->children[0], out);
        }
        if (exp->n_child > 1) {
            fprintf(out, "\tjmp L%zu\n"
                         "L%zu:\n"
                         "\tld.d $%d\n"
                         "L%zu:\n", lend2, lend, tag == t_logical_or, lend2);
        }
        break;
    }
    case t_if: {
        size_t lend = label_n++, lelse = label_n++;
        do_codegen(exp->children[0], out);
        fprintf(out, "\tcall.d abs_d\n");
        fprintf(out, "\tld.d $%lf\n"
                     "\tjl.d L%zu\n", EPS, lelse);
        do_codegen(exp->children[1], out);
        fprintf(out, "\tjmp L%zu\n"
                     "L%zu:\n", lend, lelse);
        do_codegen(exp->children[2], out);
        fprintf(out, "L%zu:\n", lend);
        break;
    }
    case t_while: {
        size_t lnext = label_n++, lend = label_n++;
        fprintf(out, "\tld.d $0\n"
                     "L%zu:\n", lnext);
        do_codegen(exp->children[0], out);
        fprintf(out, "\tcall.d abs_d\n"
                     "\tld.d $%lf\n"
                     "\tjl.d L%zu\n", EPS, lend);
        do_codegen(exp->children[1], out);
        fprintf(out, "\tswap.l\n"
                     "\tdrop.l\n"
                     "\tjmp L%zu\n"
                     "L%zu:\n", lnext, lend);
        break;
    }
    case t_statement:
        for (size_t i = 0; i < exp->n_child - 1; i++) {
            do_codegen(exp->children[i], out);
            fputs("\tdrop.l\n", out);
        }
        do_codegen(exp->children[exp->n_child - 1], out);
    }
}


static void find_vars(struct expr *exp, char ***pvars, size_t *pnvars) {
    switch(exp->tag) {
    case t_constant:
        break;
    case t_variable:
        // TODO Need hashtable for this (or more precisely symbol table)
        for (size_t i = 0; i < *pnvars; i++)
            if (!strcmp(exp->id, (*pvars)[i])) return;

        *pvars = realloc(*pvars, (1 + *pnvars)*sizeof(**pvars));
        assert(*pvars);

        (*pvars)[(*pnvars)++] = exp->id;
        break;
    default:
        for (size_t i = 0; i < exp->n_child; i++)
            find_vars(exp->children[i], pvars, pnvars);
    }
}

static void generate_variables(struct expr *exp, FILE *out) {
    char **variables = NULL;
    size_t nvars = 0;

    find_vars(exp, &variables, &nvars);

    for (size_t i = 0; i < nvars; i++)
        fprintf(out, ".local double %s\n", variables[i]);

    for (size_t i = 0; i < nvars; i++) {
        fprintf(out, "\tcall.d scan_d\n"
                     "\tst.d %s\n", variables[i]);
    }

    free(variables);
}

void generate_code(struct expr *exp, FILE *out) {
    // Declare runtime functions
    fputs(".function double power_d\n"
          ".param double arg\n"
          ".param double pow\n"
          ".function double log_d\n"
          ".param double arg\n"
          ".function double scan_d\n"
          ".function void print_d\n"
          ".param double arg\n"
          ".function double abs_d\n"
          ".param double arg\n"
          "\tld.d arg\n"
          "\tdup.l\n"
          "\tld.d $0\n"
          "\tjg.d 1\n"
          "\tneg.d\n"
          "1:\n"
          "\tret.d\n", out);

    // Declare main
    fputs(".function void main\n", out);

    // Generate and read variables
    // (all of them are locals for now and read automatically upon startup)
    generate_variables(exp, out);

    do_codegen(exp, out);
    fputs("\tcall print_d\n"
          "\tret\n", out);
}
