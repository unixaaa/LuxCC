/*
 * Simple x86 code generator
 *      IC ==> x86 ASM.
 * Of interest:
 *   => System V ABI-i386: http://www.sco.com/developers/devspecs/abi386-4.pdf
 * TOFIX:
 * - The code acts as if byte versions of ESI and EDI existed.
 */
#include "x86_cgen.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stdarg.h>
#include "../util.h"
#include "../decl.h"
#include "../expr.h"
#include "../arena.h"
#include "../imp_lim.h"
#include "../error.h"
#include "../ic.h"
#include "../dflow.h"
#include "../str.h"

#define LIVE          0
#define DEAD         -1
#define NO_NEXT_USE  -1

typedef struct QuadLiveNext QuadLiveNext;
static struct QuadLiveNext {
    char liveness[3];
    int next_use[3];
} *liveness_and_next_use;

#define tar_liveness(i)  (liveness_and_next_use[i].liveness[0])
#define arg1_liveness(i) (liveness_and_next_use[i].liveness[1])
#define arg2_liveness(i) (liveness_and_next_use[i].liveness[2])
#define tar_next_use(i)  (liveness_and_next_use[i].next_use[0])
#define arg1_next_use(i) (liveness_and_next_use[i].next_use[1])
#define arg2_next_use(i) (liveness_and_next_use[i].next_use[2])

static char *operand_liveness;
static int *operand_next_use;
static void init_operand_table(BSet *bLO);
static void compute_liveness_and_next_use(unsigned fn);
static void print_liveness_and_next_use(unsigned fn);

/*
 * Initialize the operand table with the liveness
 * and next use as of the end of the block.
 * Liveness is determined using the LiveOut set.
 */
void init_operand_table(BSet *bLO)
{
    int i;

    memset(operand_liveness, DEAD, nid_counter*sizeof(char));
    memset(operand_next_use, NO_NEXT_USE, nid_counter*sizeof(int));
    for (i = bset_iterate(bLO); i != -1; i = bset_iterate(NULL)) {
        operand_liveness[i] = LIVE;
        /*operand_next_use[i] = NO_NEXT_USE;*/
    }
}

void compute_liveness_and_next_use(unsigned fn)
{
    int b;
    unsigned entry_bb, last_bb;

    if (cg_node_is_empty(fn))
        return;

    entry_bb = cg_node(fn).bb_i;
    last_bb = cg_node(fn).bb_f;

    /* annotate the quads of every block with liveness and next-use information */
    for (b = entry_bb; b <= last_bb; b++) {
        int i;

        init_operand_table(cfg_node(b).LiveOut);

        /* scan backward through the block */
        for (i = cfg_node(b).last; i >= (int)cfg_node(b).leader; i--) {
            unsigned tar, arg1, arg2;

            tar = instruction(i).tar;
            arg1 = instruction(i).arg1;
            arg2 = instruction(i).arg2;
            switch (instruction(i).op) {
#define update_tar()\
        tar_liveness(i) = operand_liveness[address_nid(tar)],\
        tar_next_use(i) = operand_next_use[address_nid(tar)],\
        operand_liveness[address_nid(tar)] = DEAD,\
        operand_next_use[address_nid(tar)] = NO_NEXT_USE
#define update_arg1()\
        arg1_liveness(i) = operand_liveness[address_nid(arg1)],\
        arg1_next_use(i) = operand_next_use[address_nid(arg1)],\
        operand_liveness[address_nid(arg1)] = LIVE,\
        operand_next_use[address_nid(arg1)] = i
#define update_arg2()\
        arg2_liveness(i) = operand_liveness[address_nid(arg2)],\
        arg2_next_use(i) = operand_next_use[address_nid(arg2)],\
        operand_liveness[address_nid(arg2)] = LIVE,\
        operand_next_use[address_nid(arg2)] = i

            case OpAdd: case OpSub: case OpMul: case OpDiv:
            case OpRem: case OpSHL: case OpSHR: case OpAnd:
            case OpOr: case OpXor: case OpEQ: case OpNEQ:
            case OpLT: case OpLET: case OpGT: case OpGET:
                update_tar();
                if (nonconst_addr(arg1))
                    update_arg1();
                if (nonconst_addr(arg2))
                    update_arg2();
                continue;

            case OpNeg: case OpCmpl: case OpNot: case OpCh:
            case OpUCh: case OpSh: case OpUSh: case OpAsn:
                update_tar();
                if (nonconst_addr(arg1))
                    update_arg1();
                continue;

            case OpArg:
            case OpRet:
                if (nonconst_addr(arg1))
                    update_arg1();
                continue;

            case OpAddrOf:
                update_tar();
                continue;

            case OpInd: {
                int j;
                BSet *s;

                update_tar();
                update_arg1();

                if ((s=get_pointer_targets(i, address_nid(arg1))) != NULL) {
                    for (j = bset_iterate(s); j != -1; j = bset_iterate(NULL))
                        operand_liveness[j] = LIVE;
                } else {
                    /*
                     * TBD: if temporaries cannot cross basic block boundaries,
                     * only programmer declared variables need to be marked 'LIVE'.
                     */
                    memset(operand_liveness, LIVE, nid_counter*sizeof(char));
                }
            }
                continue;

            case OpIndAsn:
                tar_liveness(i) = operand_liveness[address_nid(tar)];
                tar_next_use(i) = operand_next_use[address_nid(tar)];
                operand_liveness[address_nid(tar)] = LIVE;
                operand_next_use[address_nid(tar)] = i;
                if (nonconst_addr(arg1))
                    update_arg1();
                continue;

            case OpCall:
                // if (tar)
                    // update_tar();
                // continue;

            case OpIndCall:
                if (tar)
                    update_tar();
                if (nonconst_addr(arg1))
                    update_arg1();
                continue;

            case OpCBr:
                if (nonconst_addr(tar)) {
                    tar_liveness(i) = operand_liveness[address_nid(tar)];
                    tar_next_use(i) = operand_next_use[address_nid(tar)];
                    operand_liveness[address_nid(tar)] = LIVE;
                    operand_next_use[address_nid(tar)] = i;
                }
                continue;

            default: /* other */
                continue;
            } /* switch (instruction(i).op) */
        } /* instructions */
    } /* basic blocks */

// #if DEBUG
    print_liveness_and_next_use(fn);
// #endif
}

void print_liveness_and_next_use(unsigned fn)
{
    int b;
    unsigned entry_bb, last_bb;

    entry_bb = cg_node(fn).bb_i;
    last_bb = cg_node(fn).bb_f;

    for (b = entry_bb; b <= last_bb; b++) {
        int i;

        printf("Block %d\n", b);

        for (i = cfg_node(b).leader; i <= cfg_node(b).last; i++) {
            unsigned tar, arg1, arg2;

            tar = instruction(i).tar;
            arg1 = instruction(i).arg1;
            arg2 = instruction(i).arg2;

            switch (instruction(i).op) {
#define print_tar()\
        printf("name=%s, ", address_sid(tar)),\
        printf("status=%s, ", (tar_liveness(i)==LIVE)?"LIVE":"DEAD"),\
        printf("next use=%d", tar_next_use(i))
#define print_arg1()\
        printf("name=%s, ", address_sid(arg1)),\
        printf("status=%s, ", (arg1_liveness(i)==LIVE)?"LIVE":"DEAD"),\
        printf("next use=%d", arg1_next_use(i))
#define print_arg2()\
        printf("name=%s, ", address_sid(arg2)),\
        printf("status=%s, ", (arg2_liveness(i)==LIVE)?"LIVE":"DEAD"),\
        printf("next use=%d", arg2_next_use(i))

            case OpAdd: case OpSub: case OpMul: case OpDiv:
            case OpRem: case OpSHL: case OpSHR: case OpAnd:
            case OpOr: case OpXor: case OpEQ: case OpNEQ:
            case OpLT: case OpLET: case OpGT: case OpGET:
                print_tar();
                if (nonconst_addr(arg1)) {
                    printf(" | ");
                    print_arg1();
                }
                if (nonconst_addr(arg2)) {
                    printf(" | ");
                    print_arg2();
                }
                break;

            case OpNeg: case OpCmpl: case OpNot: case OpCh:
            case OpUCh: case OpSh: case OpUSh: case OpAsn:
                print_tar();
                if (nonconst_addr(arg1)) {
                    printf(" | ");
                    print_arg1();
                }
                break;

            case OpArg:
            case OpRet:
                if (nonconst_addr(arg1)) {
                    print_arg1();
                }
                break;

            case OpAddrOf:
                print_tar();
                break;

            case OpInd:
                print_tar();
                printf(" | ");
                print_arg1();
                break;

            case OpIndAsn:
                print_tar();
                if (nonconst_addr(arg1)) {
                    printf(" | ");
                    print_arg1();
                }
                break;

            case OpCall:
                // if (tar)
                    // print_tar();
                // break;

            case OpIndCall:
                if (tar)
                    print_tar();
                if (nonconst_addr(arg1)) {
                    printf(" | ");
                    print_arg1();
                }
                break;

            case OpCBr:
                if (nonconst_addr(tar))
                    print_tar();
                break;
            default:
                continue;
            } /* instructions */
            printf("\n--------------\n");
        } /* basic blocks */
        printf("\n\n");
    }
}
/*
 * ===
 */

typedef enum {
    X86_EAX,
    X86_EBX,
    X86_ECX,
    X86_EDX,
    X86_ESI,
    X86_EDI,
    X86_NREG,
} X86_Reg;

static int pinned[X86_NREG];
static int modified[X86_NREG];
#define pin_reg(r)      pinned[r] = TRUE;
#define unpin_reg(r)    pinned[r] = FALSE;

static char *x86_reg_str[] = {
    "eax",
    "ebx",
    "ecx",
    "edx",
    "esi",
    "edi",
};

static char *x86_lwreg_str[] = {
    "ax",
    "bx",
    "cx",
    "dx",
    "si",
    "di",
};

static char *x86_lbreg_str[] = {
    "al",
    "bl",
    "cl",
    "dl",
    "??",
    "??",
};

static int size_of_local_area;
static char *curr_func;
static unsigned temp_struct_size;
static int big_return;
static unsigned arg_nb;
static int calls_to_fix_counter;
static char *calls_to_fix[64];
static int string_literal_counter;
static int new_string_literal(unsigned a);

typedef struct Temp Temp;
struct Temp {
    int nid;
    int offs;      /* offset from ebp */
    int used;      /* used or free */
    Temp *next;
} *temp_list;

int get_temp_offs(unsigned a)
{
    Temp *p;

    /* see if it was already allocated */
    for (p = temp_list; p != NULL; p = p->next)
        if (p->used && p->nid==address_nid(a))
            return p->offs;

    /* try to find an unused temp */
    for (p = temp_list; p != NULL; p = p->next) {
        if (!p->used) {
            p->nid = address_nid(a);
            p->used = TRUE;
            return p->offs;
        }
    }

    /* allocate a new temp */
    p = malloc(sizeof(Temp));
    p->nid = address_nid(a);
    size_of_local_area -= 4;
    p->offs = size_of_local_area;
    p->used = TRUE;
    p->next = temp_list;
    temp_list = p;
    return p->offs;
}

void free_temp(unsigned a)
{
    Temp *p;

    for (p = temp_list; p != NULL; p = p->next) {
        if (p->used && p->nid==address_nid(a)) {
            p->used = FALSE;
            break;
        }
    }
}

void free_all_temps(void)
{
    Temp *p, *q;

    p = temp_list;
    while (p != NULL) {
        q = p;
        p = p->next;
        free(q);
    }
    temp_list = NULL;
}

typedef struct AddrDescr AddrDescr;
struct AddrDescr {
    int in_reg;
    X86_Reg r;
} *addr_descr_tab;

static
void init_addr_descr_tab(void)
{
    addr_descr_tab = calloc(nid_counter, sizeof(AddrDescr));
}

#define addr_in_reg(a)  (addr_descr_tab[address_nid(a)].in_reg)
#define addr_reg(a)     (addr_descr_tab[address_nid(a)].r)

#define MAX_ADDRS_PER_REG 10
typedef struct RegDescr RegDescr;
struct RegDescr {
    int naddrs; /* # of addresses this register is housing */
    unsigned addrs[MAX_ADDRS_PER_REG];
} reg_descr_tab[X86_NREG];

void reg_descr_add_addr(X86_Reg r, unsigned a)
{
    int i;

    assert(reg_descr_tab[r].naddrs < MAX_ADDRS_PER_REG);

    /* [?] search before add a new address */

    for (i = 0; i < MAX_ADDRS_PER_REG; i++) {
        if (!reg_descr_tab[r].addrs[i]) {
            reg_descr_tab[r].addrs[i] = a;
            reg_descr_tab[r].naddrs++;
            break;
        }
    }
}

void reg_descr_rem_addr(X86_Reg r, unsigned a)
{
    int i;
    int naddrs;

    i = 0;
    naddrs = reg_descr_tab[r].naddrs;
    while (naddrs && i<MAX_ADDRS_PER_REG) {
        unsigned a2;

        if ((a2=reg_descr_tab[r].addrs[i]) != 0) {
            --naddrs;
            if (address_nid(a) == address_nid(a2)) {
                reg_descr_tab[r].addrs[i] = 0;
                reg_descr_tab[r].naddrs--;
                break;
            }
        }
        ++i;
    }
}

void clear_reg_descr(X86_Reg r)
{
    int i;

    if (reg_descr_tab[r].naddrs == 0)
        return;

    for (i = 0; i < MAX_ADDRS_PER_REG; i++)
        reg_descr_tab[r].addrs[i] = 0;
    reg_descr_tab[r].naddrs = 0;
}

static String *func_body, *func_prolog, *func_epilog;
#define emit(...)   (string_printf(func_body, __VA_ARGS__))
#define emitln(...) (string_printf(func_body, __VA_ARGS__), string_printf(func_body, "\n"))
#define emit_prolog(...)   (string_printf(func_prolog, __VA_ARGS__))
#define emit_prologln(...) (string_printf(func_prolog, __VA_ARGS__), string_printf(func_prolog, "\n"))
#define emit_epilog(...)   (string_printf(func_epilog, __VA_ARGS__))
#define emit_epilogln(...) (string_printf(func_epilog, __VA_ARGS__), string_printf(func_epilog, "\n"))

int new_string_literal(unsigned a)
{
    char *s;
    unsigned len, i;

    emitln("segment .rodata");
    emitln("_@S%d:", string_literal_counter);

    s = address(a).cont.str;
    len = strlen(s)+1;
    for (i = len/4; i; i--) {
        emitln("dd 0x%x%x%x%x", s[3], s[2], s[1], s[0]);
        s += 4;
    }
    for (i = len%4; i; i--)
        emitln("db 0x%x", *s++);

    emitln("segment .text");
    return string_literal_counter++;
}

void x86_function_definition(TypeExp *decl_specs, TypeExp *header);

void x86_cgen(void)
{
    unsigned i;
    ExternId *ed, *func_def_list[128] = { NULL };

    for (ed = get_extern_symtab(), i = 0; ed != NULL; ed = ed->next) {
        if (ed->status == REFERENCED) {
            ;
        } else {
            if (ed->declarator->child!=NULL && ed->declarator->child->op==TOK_FUNCTION)
                func_def_list[i++] = ed;
            else
                ;
        }
    }

    /* generate intermediate code and do some analysis */
    ic_main(func_def_list); //exit(0);

    /* compute liveness and next use */
    liveness_and_next_use = malloc(ic_instructions_counter*sizeof(QuadLiveNext));
    operand_liveness = malloc(nid_counter*sizeof(char));
    operand_next_use = malloc(nid_counter*sizeof(int));
    for (i = 0; i < cg_nodes_counter; i++)
        compute_liveness_and_next_use(i);
    free(operand_liveness);
    free(operand_next_use);

    /* generate assembly for functions */
    func_body = string_new(1024);
    func_prolog = string_new(1024);
    func_epilog = string_new(1024);
    for (i = 0; (ed=func_def_list[i]) != NULL; i++)
        x86_function_definition(ed->decl_specs, ed->declarator);
    string_free(func_body);
    string_free(func_prolog);
    string_free(func_epilog);

    printf("\n");
    for (i = 0; i < X86_NREG; i++)
        printf("reg%d = %d\n", i, reg_descr_tab[i].naddrs);
}

static X86_Reg get_empty_reg(void);
static void x86_load(X86_Reg r, unsigned a);
static char *get_operand(unsigned a);
static void x86_store(X86_Reg r, unsigned a);
static void spill_reg(X86_Reg r);
static X86_Reg get_reg(int intr);

X86_Reg get_empty_reg(void)
{
    int i;

    for (i = 0; i < X86_NREG; i++) {
        if (!pinned[i] && reg_descr_tab[i].naddrs==0) {
            modified[i] = TRUE;
            return (X86_Reg)i;
        }
    }
    return -1; /* couldn't find any */
}

X86_Reg get_unpinned_reg(void)
{
    int i;

    for (i = 0; i < X86_NREG; i++) {
        if (!pinned[i]) {
            modified[i] = TRUE;
            return (X86_Reg)i;
        }
    }
    return -1;
}

void spill_reg(X86_Reg r)
{
    int i;

    if (reg_descr_tab[r].naddrs == 0)
        return;

    for (i = 0; i < MAX_ADDRS_PER_REG; i++) {
        unsigned a;

        if (!(a=reg_descr_tab[r].addrs[i]))
            continue;
        x86_store(r, a);
        addr_in_reg(a) = FALSE;
        reg_descr_tab[r].addrs[i] = 0;
        reg_descr_tab[r].naddrs--;
    }
}

void spill_all(void)
{
    int i;

    for (i = 0; i < X86_NREG; i++)
        spill_reg((X86_Reg)i);
}

X86_Reg get_reg(int i)
{
    X86_Reg r;
    unsigned arg1;

    arg1 = instruction(i).arg1;
    if (address(arg1).kind==IdKind || address(arg1).kind==TempKind) {
        if (addr_in_reg(arg1) && arg1_liveness(i)==DEAD) {
            /* if the register doesn't hold some other address, we are done */
            r = addr_reg(arg1);
            if (reg_descr_tab[r].naddrs == 1)
                return r;
        }
    }

    /* find an empty register */
    if ((r=get_empty_reg()) != -1)
        return r;

    /* choose an unpinned register and spill its contents */
    r = get_unpinned_reg();
    assert(r != -1);
    spill_reg(r);
    return r;
}

#define offset(a) (address(a).cont.var.offset)

char *get_operand(unsigned a)
{
    static char op[128];

    if (address(a).kind == IConstKind) {
        sprintf(op, "%lu", address(a).cont.uval);
    } else if (address(a).kind == StrLitKind) {
        sprintf(op, "_@S%d", new_string_literal(a));
    } else if (address(a).kind == IdKind) {
        ExecNode *e;

        if (addr_in_reg(a))
            return x86_reg_str[addr_reg(a)];

        e = address(a).cont.var.e;
        switch (get_type_category(&e->type)) {
        case TOK_STRUCT:
        case TOK_UNION:
            /*assert(0);*/
        case TOK_SUBSCRIPT:
        case TOK_FUNCTION:
            if (e->attr.var.duration == DURATION_STATIC) {
                if (e->attr.var.linkage == LINKAGE_NONE)
                    sprintf(op, "%s@%s", curr_func, e->attr.str);
                else
                    sprintf(op, "%s", e->attr.str);
                return op;
            } else {
                X86_Reg r;

                if ((r=get_empty_reg()) == -1) {
                    r = get_unpinned_reg();
                    assert(r != -1);
                    spill_reg(r);
                }
                if (e->attr.var.is_param)
                    emitln("lea %s, [ebp+%d]", x86_reg_str[r], offset(a));
                else
                    emitln("lea %s, [ebp-%d]", x86_reg_str[r], -offset(a));
                return x86_reg_str[r];
            }
        case TOK_SHORT:
        case TOK_UNSIGNED_SHORT:
        case TOK_CHAR:
        case TOK_SIGNED_CHAR:
        case TOK_UNSIGNED_CHAR: { /* promote to dword */
            X86_Reg r;

            if ((r=get_empty_reg()) == -1) {
                r = get_unpinned_reg();
                assert(r != -1);
                spill_reg(r);
            }
            x86_load(r, a);
            return x86_reg_str[r];
        }
        default: /* dword sized, OK */
            break;
        }

        /* fall through (dword sized operand) */
        if (e->attr.var.duration == DURATION_STATIC) {
            if (e->attr.var.linkage == LINKAGE_NONE)
                sprintf(op, "dword [%s@%s]", curr_func, e->attr.str);
            else
                sprintf(op, "dword [%s]", e->attr.str);
        } else {
            if (e->attr.var.is_param)
                sprintf(op, "dword [ebp+%d]", offset(a));
            else
                sprintf(op, "dword [ebp-%d]", -offset(a));
        }
    } else if (address(a).kind == TempKind) {
        if (addr_in_reg(a))
            return x86_reg_str[addr_reg(a)];
        else
            sprintf(op, "dword [ebp-%d]", -get_temp_offs(a));
    }

    return op;
}

void x86_load_addr(X86_Reg r, unsigned a)
{
    ExecNode *e;

    e = address(a).cont.var.e;
    if (e->attr.var.duration == DURATION_STATIC) {
        if (e->attr.var.linkage == LINKAGE_NONE)
            emitln("mov %s, %s@%s", x86_reg_str[r], curr_func, e->attr.str);
        else
            emitln("mov %s, %s", x86_reg_str[r], e->attr.str);
    } else {
        if (e->attr.var.is_param)
            emitln("lea %s, [ebp+%d]", x86_reg_str[r], offset(a));
        else
            emitln("lea %s, [ebp-%d]", x86_reg_str[r], -offset(a));
    }
}

void x86_load(X86_Reg r, unsigned a)
{
    if (address(a).kind == IConstKind) {
        emitln("mov %s, %lu", x86_reg_str[r], address(a).cont.uval);
    } else if (address(a).kind == StrLitKind) {
        emitln("mov %s, _@S%d", x86_reg_str[r], new_string_literal(a));
    } else if (address(a).kind == IdKind) {
        ExecNode *e;
        char *siz_str, *mov_str;

        if (addr_in_reg(a)) {
            if (addr_reg(a) == r)
                ; /* already in the register */
            else
                emitln("mov %s, %s", x86_reg_str[r], x86_reg_str[addr_reg(a)]);
            return;
        }

        e = address(a).cont.var.e;
        switch (get_type_category(&e->type)) {
        case TOK_STRUCT:
        case TOK_UNION:
        case TOK_SUBSCRIPT:
        case TOK_FUNCTION:
            x86_load_addr(r, a);
            return;
        case TOK_SHORT:
            mov_str = "movsx";
            siz_str = "word";
            break;
        case TOK_UNSIGNED_SHORT:
            mov_str = "movzx";
            siz_str = "word";
            break;
        case TOK_CHAR:
        case TOK_SIGNED_CHAR:
            mov_str = "movsx";
            siz_str = "byte";
            break;
        case TOK_UNSIGNED_CHAR:
            mov_str = "movzx";
            siz_str = "byte";
            break;
        default:
            mov_str = "mov";
            siz_str = "dword";
            break;
        }

        if (e->attr.var.duration == DURATION_STATIC) {
            if (e->attr.var.linkage == LINKAGE_NONE) /* static local */
                emitln("%s %s, %s [%s@%s]", mov_str, x86_reg_str[r], siz_str, curr_func, e->attr.str);
            else /* global */
                emitln("%s %s, %s [%s]", mov_str, x86_reg_str[r], siz_str, e->attr.str);
        } else { /* parameter or local */
            if (e->attr.var.is_param)
                emitln("%s %s, %s [ebp+%d]", mov_str, x86_reg_str[r], siz_str, offset(a));
            else
                emitln("%s %s, %s [ebp-%d]", mov_str, x86_reg_str[r], siz_str, -offset(a));
        }
    } else if (address(a).kind == TempKind) {
        if (addr_in_reg(a)) {
            if (addr_reg(a) == r)
                return; /* already in the register */
            else
                emitln("mov %s, %s", x86_reg_str[r], x86_reg_str[addr_reg(a)]);
        } else {
            emitln("mov %s, dword [ebp-%d]", x86_reg_str[r], -get_temp_offs(a));
        }
    }
}

void x86_store(X86_Reg r, unsigned a)
{
    if (address(a).kind == IdKind) {
        ExecNode *e;
        char *siz_str, *reg_str;

        e = address(a).cont.var.e;
        switch (get_type_category(&e->type)) {
        case TOK_STRUCT:
        case TOK_UNION: {
            int cluttered;

            cluttered = 0;
            if (r != X86_ESI) {
                if (reg_descr_tab[X86_ESI].naddrs != 0) {
                    cluttered |= 1;
                    emitln("push esi");
                }
                emitln("mov esi, %s", x86_reg_str[r]);
            }
            if (!addr_in_reg(a) || addr_reg(a)!=X86_EDI) {
                if (reg_descr_tab[X86_EDI].naddrs != 0) {
                    cluttered |= 2;
                    emitln("push edi");
                }
                x86_load_addr(X86_EDI, a);
            }
            if (reg_descr_tab[X86_ECX].naddrs != 0) {
                cluttered |= 4;
                emitln("push ecx");
            }
            emitln("mov ecx, %u", compute_sizeof(&e->type));
            /*emitln("cld");*/
            emitln("rep movsb");
            /* restore all */
            if (cluttered & 4)
                emitln("pop ecx");
            if (cluttered & 2)
                emitln("pop edi");
            if (cluttered & 1)
                emitln("pop esi");
        }
            return;
        case TOK_SHORT:
        case TOK_UNSIGNED_SHORT:
            siz_str = "word";
            reg_str = x86_lwreg_str[r];
            break;
        case TOK_CHAR:
        case TOK_SIGNED_CHAR:
        case TOK_UNSIGNED_CHAR:
            siz_str = "byte";
            reg_str = x86_lbreg_str[r];
            break;
        default:
            siz_str = "dword";
            reg_str = x86_reg_str[r];
            break;
        }

        if (e->attr.var.duration == DURATION_STATIC) {
            if (e->attr.var.linkage == LINKAGE_NONE) /* static local */
                emitln("mov %s [%s@%s], %s", siz_str, curr_func, e->attr.str, reg_str);
            else /* global */
                emitln("mov %s [%s], %s", siz_str, e->attr.str, reg_str);
        } else { /* parameter or local */
            if (e->attr.var.is_param)
                emitln("mov %s [ebp+%d], %s", siz_str, offset(a), reg_str);
            else
                emitln("mov %s [ebp-%d], %s", siz_str, -offset(a), reg_str);
        }
    } else if (address(a).kind == TempKind) {
        emitln("mov dword [ebp-%d], %s", -get_temp_offs(a), x86_reg_str[r]);
    }
}

void update_arg_descriptors(unsigned arg, unsigned char liveness, int next_use)
{
    /* If arg is in a register r and arg has no next use, then
       a) If arg is LIVE, generate spill code to move the value of r to memory
       location of arg.
       b) Mark the register and address descriptor tables to indicate that the
       register r no longer contains the value of arg. */
    if ((address(arg).kind!=IdKind && address(arg).kind!=TempKind) || next_use!=NO_NEXT_USE)
        return;

    if (addr_in_reg(arg)) {
        if (liveness == LIVE) { /* spill */
            x86_store(addr_reg(arg), arg);
            addr_in_reg(arg) = FALSE;
        } else {
            if (address(arg).kind == TempKind)
                free_temp(arg);
        }
        reg_descr_rem_addr(addr_reg(arg), arg);
    } else {
        if (liveness == LIVE) {
            ;
        } else {
            if (address(arg).kind == TempKind)
                free_temp(arg);
        }
    }
}

void update_tar_descriptors(X86_Reg res, unsigned tar, unsigned char liveness, int next_use)
{
    /* update the address descriptor table to indicate
       that the value of tar is stored in res only */
    addr_in_reg(tar) = TRUE;
    addr_reg(tar) = res;

    /* update the register descriptor table to indicate that
       res contains the value of tar only */
    clear_reg_descr(res);
    reg_descr_add_addr(res, tar);

    if (next_use == NO_NEXT_USE) {
        if (liveness == LIVE) { /* spill */
            x86_store(res, tar);
            addr_in_reg(tar) = FALSE;
        }
        clear_reg_descr(res);
    }
}

void compare_against_zero(unsigned a)
{
    if (address(a).kind == IConstKind) {
        assert(0); /* can be folded */
    } else if (address(a).kind == StrLitKind) {
        assert(0); /* can be folded */
    } else if (address(a).kind == IdKind) {
        ExecNode *e;
        char *siz_str;

        if (addr_in_reg(a)) {
            emitln("cmp %s, 0", x86_reg_str[addr_reg(a)]);
            return;
        }

        e = address(a).cont.var.e;
        switch (get_type_category(&e->type)) {
        case TOK_SHORT:
        case TOK_UNSIGNED_SHORT:
            siz_str = "word";
            break;
        case TOK_CHAR:
        case TOK_SIGNED_CHAR:
        case TOK_UNSIGNED_CHAR:
            siz_str = "byte";
            break;
        default:
            siz_str = "dword";
            break;
        }

        if (e->attr.var.duration == DURATION_STATIC) {
            if (e->attr.var.linkage == LINKAGE_NONE)
                emitln("cmp %s [%s@%s], 0", siz_str, curr_func, e->attr.str);
            else
                emitln("cmp %s [%s], 0", siz_str, e->attr.str);
        } else {
            if (e->attr.var.is_param)
                emitln("cmp %s [ebp+%d], 0", siz_str, offset(a));
            else
                emitln("cmp %s [ebp-%d], 0", siz_str, -offset(a));
        }
    } else if (address(a).kind == TempKind) {
        if (addr_in_reg(a))
            emitln("cmp %s, 0", x86_reg_str[addr_reg(a)]);
        else
            emitln("cmp dword [ebp-%d], 0", -get_temp_offs(a));
    }
}

#define UPDATE_ADDRESSES(res_reg) {\
    update_tar_descriptors(res_reg, tar, tar_liveness(i), tar_next_use(i));\
    update_arg_descriptors(arg1, arg1_liveness(i), arg1_next_use(i));\
    update_arg_descriptors(arg2, arg2_liveness(i), arg2_next_use(i));\
}
#define UPDATE_ADDRESSES_UNARY(res_reg) {\
    update_tar_descriptors(res_reg, tar, tar_liveness(i), tar_next_use(i));\
    update_arg_descriptors(arg1, arg1_liveness(i), arg1_next_use(i));\
}

static void x86_pre_call(int i);
static void x86_div_rem(X86_Reg res, int i, unsigned tar, unsigned arg1, unsigned arg2);

void x86_add(int i, unsigned tar, unsigned arg1, unsigned arg2)
{
    X86_Reg res;

    res = get_reg(i);
    x86_load(res, arg1);
    pin_reg(res);
    emitln("add %s, %s", x86_reg_str[res], get_operand(arg2));
    unpin_reg(res);
    UPDATE_ADDRESSES(res);
}
void x86_sub(int i, unsigned tar, unsigned arg1, unsigned arg2)
{
    X86_Reg res;

    res = get_reg(i);
    x86_load(res, arg1);
    pin_reg(res);
    emitln("sub %s, %s", x86_reg_str[res], get_operand(arg2));
    unpin_reg(res);
    UPDATE_ADDRESSES(res);
}
void x86_mul(int i, unsigned tar, unsigned arg1, unsigned arg2)
{
    X86_Reg res;

    res = get_reg(i);
    x86_load(res, arg1);
    pin_reg(res);
    emitln("imul %s, %s", x86_reg_str[res], get_operand(arg2));
    unpin_reg(res);
    UPDATE_ADDRESSES(res);
}
void x86_div_rem(X86_Reg res, int i, unsigned tar, unsigned arg1, unsigned arg2)
{
    char *instr, *divop;

    if (get_reg(i) != X86_EAX)
        spill_reg(X86_EAX);
    x86_load(X86_EAX, arg1);
    pin_reg(X86_EAX);
    spill_reg(X86_EDX);
    pin_reg(X86_EDX);
    if (is_unsigned_int(get_type_category(instruction(i).type))) {
        emitln("xor edx, edx");
        instr = "div";
    } else {
        emitln("cdq");
        instr = "idiv";
    }
    if (address(arg2).kind != IConstKind) {
        divop = get_operand(arg2);
    } else {
        X86_Reg r;

        if ((r=get_empty_reg()) == -1) {
            r = get_unpinned_reg();
            assert(r != -1);
            spill_reg(r);
        }
        x86_load(r, arg2);
        divop = x86_reg_str[r];
    }
    emitln("%s %s", instr, divop);
    unpin_reg(X86_EAX);
    unpin_reg(X86_EDX);
    UPDATE_ADDRESSES(res);
}
void x86_div(int i, unsigned tar, unsigned arg1, unsigned arg2)
{
    x86_div_rem(X86_EAX, i, tar, arg1, arg2);
}
void x86_rem(int i, unsigned tar, unsigned arg1, unsigned arg2)
{
    x86_div_rem(X86_EDX, i, tar, arg1, arg2);
}
void x86_shl(int i, unsigned tar, unsigned arg1, unsigned arg2)
{
    X86_Reg res;

    res = get_reg(i);
    x86_load(res, arg1);
    if (address(arg2).kind == IConstKind) {
        emitln("sal %s, %lu", x86_reg_str[res], address(arg2).cont.uval);
    } else {
        if (!addr_in_reg(arg2) || addr_reg(arg2)!=X86_ECX) {
            spill_reg(X86_ECX);
            x86_load(X86_ECX, arg2);
        }
        emitln("sal %s, cl", x86_reg_str[res]);
    }
    UPDATE_ADDRESSES(res);
}
void x86_shr(int i, unsigned tar, unsigned arg1, unsigned arg2)
{
    char *instr;
    X86_Reg res;

    instr = (is_unsigned_int(get_type_category(instruction(i).type))) ? "shr" : "sar";

    res = get_reg(i);
    x86_load(res, arg1);
    if (address(arg2).kind == IConstKind) {
        emitln("%s %s, %lu", instr, x86_reg_str[res], address(arg2).cont.uval);
    } else {
        if (!addr_in_reg(arg2) || addr_reg(arg2)!=X86_ECX) {
            spill_reg(X86_ECX);
            x86_load(X86_ECX, arg2);
        }
        emitln("%s %s, cl", instr, x86_reg_str[res]);
    }
    UPDATE_ADDRESSES(res);
}
void x86_and(int i, unsigned tar, unsigned arg1, unsigned arg2)
{
    X86_Reg res;

    res = get_reg(i);
    x86_load(res, arg1);
    pin_reg(res);
    emitln("and %s, %s", x86_reg_str[res], get_operand(arg2));
    unpin_reg(res);
    UPDATE_ADDRESSES(res);
}
void x86_or(int i, unsigned tar, unsigned arg1, unsigned arg2)
{
    X86_Reg res;

    res = get_reg(i);
    x86_load(res, arg1);
    pin_reg(res);
    emitln("or %s, %s", x86_reg_str[res], get_operand(arg2));
    unpin_reg(res);
    UPDATE_ADDRESSES(res);
}
void x86_xor(int i, unsigned tar, unsigned arg1, unsigned arg2)
{
    X86_Reg res;

    res = get_reg(i);
    x86_load(res, arg1);
    pin_reg(res);
    emitln("xor %s, %s", x86_reg_str[res], get_operand(arg2));
    unpin_reg(res);
    UPDATE_ADDRESSES(res);
}
void x86_eq(int i, unsigned tar, unsigned arg1, unsigned arg2)
{
    X86_Reg res;

    res = get_reg(i);
    x86_load(res, arg1);
    pin_reg(res);
    emitln("cmp %s, %s", x86_reg_str[res], get_operand(arg2));
    unpin_reg(res);
    emitln("sete %s", x86_lbreg_str[res]);
    emitln("movzx %s, %s", x86_reg_str[res], x86_lbreg_str[res]);
    UPDATE_ADDRESSES(res);
}
void x86_neq(int i, unsigned tar, unsigned arg1, unsigned arg2)
{
    X86_Reg res;

    res = get_reg(i);
    x86_load(res, arg1);
    pin_reg(res);
    emitln("cmp %s, %s", x86_reg_str[res], get_operand(arg2));
    unpin_reg(res);
    emitln("setne %s", x86_lbreg_str[res]);
    emitln("movzx %s, %s", x86_reg_str[res], x86_lbreg_str[res]);
    UPDATE_ADDRESSES(res);
}
void x86_lt(int i, unsigned tar, unsigned arg1, unsigned arg2)
{
    X86_Reg res;

    res = get_reg(i);
    x86_load(res, arg1);
    pin_reg(res);
    emitln("cmp %s, %s", x86_reg_str[res], get_operand(arg2));
    unpin_reg(res);
    emitln("set%s %s", ((int)instruction(i).type==IC_SIGNED)?"l":"b", x86_lbreg_str[res]);
    emitln("movzx %s, %s", x86_reg_str[res], x86_lbreg_str[res]);
    UPDATE_ADDRESSES(res);
}
void x86_let(int i, unsigned tar, unsigned arg1, unsigned arg2)
{
    X86_Reg res;

    res = get_reg(i);
    x86_load(res, arg1);
    pin_reg(res);
    emitln("cmp %s, %s", x86_reg_str[res], get_operand(arg2));
    unpin_reg(res);
    emitln("set%s %s", ((int)instruction(i).type==IC_SIGNED)?"le":"be", x86_lbreg_str[res]);
    emitln("movzx %s, %s", x86_reg_str[res], x86_lbreg_str[res]);
    UPDATE_ADDRESSES(res);
}
void x86_gt(int i, unsigned tar, unsigned arg1, unsigned arg2)
{
    X86_Reg res;

    res = get_reg(i);
    x86_load(res, arg1);
    pin_reg(res);
    emitln("cmp %s, %s", x86_reg_str[res], get_operand(arg2));
    unpin_reg(res);
    emitln("set%s %s", ((int)instruction(i).type==IC_SIGNED)?"g":"a", x86_lbreg_str[res]);
    emitln("movzx %s, %s", x86_reg_str[res], x86_lbreg_str[res]);
    UPDATE_ADDRESSES(res);
}
void x86_get(int i, unsigned tar, unsigned arg1, unsigned arg2)
{
    X86_Reg res;

    res = get_reg(i);
    x86_load(res, arg1);
    pin_reg(res);
    emitln("cmp %s, %s", x86_reg_str[res], get_operand(arg2));
    unpin_reg(res);
    emitln("set%s %s", ((int)instruction(i).type==IC_SIGNED)?"ge":"ae", x86_lbreg_str[res]);
    emitln("movzx %s, %s", x86_reg_str[res], x86_lbreg_str[res]);
    UPDATE_ADDRESSES(res);
}

void x86_neg(int i, unsigned tar, unsigned arg1, unsigned arg2)
{
    X86_Reg res;

    res = get_reg(i);
    x86_load(res, arg1);
    emitln("neg %s", x86_reg_str[res]);
    UPDATE_ADDRESSES_UNARY(res);
}
void x86_cmpl(int i, unsigned tar, unsigned arg1, unsigned arg2)
{
    X86_Reg res;

    res = get_reg(i);
    x86_load(res, arg1);
    emitln("not %s", x86_reg_str[res]);
    UPDATE_ADDRESSES_UNARY(res);
}
void x86_not(int i, unsigned tar, unsigned arg1, unsigned arg2)
{
    X86_Reg res;

    res = get_reg(i);
    x86_load(res, arg1);
    emitln("cmp %s, 0", x86_reg_str[res]);
    emitln("sete %s", x86_lbreg_str[res]);
    emitln("movzx %s, %s", x86_reg_str[res], x86_lbreg_str[res]);
    UPDATE_ADDRESSES_UNARY(res);
}
void x86_ch(int i, unsigned tar, unsigned arg1, unsigned arg2)
{
    X86_Reg res;

    /* TODO: handle the case where the register is esi or edi */

    res = get_reg(i);
    x86_load(res, arg1);
    emitln("movsx %s, %s", x86_reg_str[res], x86_lbreg_str[res]);
    UPDATE_ADDRESSES_UNARY(res);
}
void x86_uch(int i, unsigned tar, unsigned arg1, unsigned arg2)
{
    X86_Reg res;

    /* TODO: handle the case where the register is esi or edi */

    res = get_reg(i);
    x86_load(res, arg1);
    emitln("movzx %s, %s", x86_reg_str[res], x86_lbreg_str[res]);
    UPDATE_ADDRESSES_UNARY(res);
}
void x86_sh(int i, unsigned tar, unsigned arg1, unsigned arg2)
{
    X86_Reg res;

    res = get_reg(i);
    x86_load(res, arg1);
    emitln("movsx %s, %s", x86_reg_str[res], x86_lwreg_str[res]);
    UPDATE_ADDRESSES_UNARY(res);
}
void x86_ush(int i, unsigned tar, unsigned arg1, unsigned arg2)
{
    X86_Reg res;

    res = get_reg(i);
    x86_load(res, arg1);
    emitln("movzx %s, %s", x86_reg_str[res], x86_lwreg_str[res]);
    UPDATE_ADDRESSES_UNARY(res);
}
void x86_addr_of(int i, unsigned tar, unsigned arg1, unsigned arg2)
{
    X86_Reg res;

    res = get_reg(i);
    x86_load_addr(res, arg1);
    UPDATE_ADDRESSES_UNARY(res);
}

void x86_ind(int i, unsigned tar, unsigned arg1, unsigned arg2)
{
    BSet *s;
    X86_Reg res;
    char *reg_str;

    /* spill any target currently in a register */
    if ((s=get_pointer_targets(i, address_nid(arg1))) != NULL) {
        int j;

        for (j = bset_iterate(s); j != -1; j = bset_iterate(NULL))
            if (addr_descr_tab[j].in_reg)
                spill_reg(addr_descr_tab[j].r);
    } else {
        spill_all();
    }

    res = get_reg(i);
    x86_load(res, arg1);
    reg_str = x86_reg_str[res];
    switch (get_type_category(instruction(i).type)) {
    case TOK_STRUCT:
    case TOK_UNION:
        break;
    case TOK_SHORT:
        emitln("movsx %s, word [%s]", reg_str, reg_str);
        break;
    case TOK_UNSIGNED_SHORT:
        emitln("movzx %s, word [%s]", reg_str, reg_str);
        break;
    case TOK_CHAR:
    case TOK_SIGNED_CHAR:
        emitln("movsx %s, byte [%s]", reg_str, reg_str);
        break;
    case TOK_UNSIGNED_CHAR:
        emitln("movzx %s, byte [%s]", reg_str, reg_str);
        break;
    default:
        emitln("mov %s, dword [%s]", reg_str, reg_str);
        break;
    }
    UPDATE_ADDRESSES_UNARY(res);
}
void x86_asn(int i, unsigned tar, unsigned arg1, unsigned arg2)
{
    X86_Reg res;

    res = get_reg(i);
    x86_load(res, arg1);
    UPDATE_ADDRESSES_UNARY(res);
}

void x86_pre_call(int i)
{
    Token cat;

    spill_all();
    if ((cat=get_type_category(instruction(i).type))==TOK_STRUCT || cat==TOK_UNION) {
        unsigned siz;

        siz = compute_sizeof(instruction(i).type);
        if (siz > temp_struct_size)
            temp_struct_size = siz;
        emit("lea eax, [ebp-");
        calls_to_fix[calls_to_fix_counter++] = string_curr(func_body);
        emitln("XXXXXXXXXXXXXXXX");
        emitln("push eax");
    }
}
void x86_indcall(int i, unsigned tar, unsigned arg1, unsigned arg2)
{
    x86_pre_call(i);
    emitln("call %s", get_operand(arg1));
    if (arg_nb)
        emitln("add esp, %u", arg_nb);
    arg_nb = 0;
    if (tar)
        update_tar_descriptors(X86_EAX, tar, tar_liveness(i), tar_next_use(i));
    update_arg_descriptors(arg1, arg1_liveness(i), arg1_next_use(i));
}
void x86_call(int i, unsigned tar, unsigned arg1, unsigned arg2)
{
    x86_pre_call(i);
    emitln("call %s", address(arg1).cont.var.e->attr.str);
    if (arg_nb)
        emitln("add esp, %u", arg_nb);
    arg_nb = 0;
    if (tar)
        update_tar_descriptors(X86_EAX, tar, tar_liveness(i), tar_next_use(i));
    /*update_arg_descriptors(arg1, instruction(i).liveness[1], instruction(i).next_use[1]);*/
}

#define emit_lab(n)         emitln("_@L%ld:", n)
#define emit_jmp(target)    emitln("jmp _@L%ld", target)
#define emit_jmpeq(target)  emitln("je _@L%ld", target)
#define emit_jmpneq(target) emitln("jne _@L%ld", target)

void x86_ind_asn(int i, unsigned tar, unsigned arg1, unsigned arg2)
{
    BSet *s;
    Token ty;
    X86_Reg res, pr;
    char *siz_str, *reg_str;

    /* force the reload of any target currently in a register */
    if ((s=get_pointer_targets(i, address_nid(tar))) != NULL) {
        int j;

        for (j = bset_iterate(s); j != -1; j = bset_iterate(NULL))
            if (addr_descr_tab[j].in_reg)
                spill_reg(addr_descr_tab[j].r);
    } else {
        spill_all();
    }

    ty = get_type_category(instruction(i).type);
    if (ty==TOK_STRUCT || ty==TOK_UNION) {
        int cluttered;

        cluttered = 0;
        if (!addr_in_reg(arg1) || addr_reg(arg1)!=X86_ESI) {
            if (reg_descr_tab[X86_ESI].naddrs != 0) {
                cluttered |= 1;
                emitln("push esi");
            }
            x86_load(X86_ESI, arg1);
        }
        if (!addr_in_reg(tar) || addr_reg(tar)!=X86_EDI) {
            if (reg_descr_tab[X86_EDI].naddrs != 0) {
                cluttered |= 2;
                emitln("push edi");
            }
            x86_load(X86_EDI, tar);
        }
        if (reg_descr_tab[X86_ECX].naddrs != 0) {
            cluttered |= 4;
            emitln("push ecx");
        }
        emitln("mov ecx, %u", compute_sizeof(instruction(i).type));
        /*emitln("cld");*/
        emitln("rep movsb");
        if (cluttered & 4)
            emitln("pop ecx");
        if (cluttered & 2)
            emitln("pop edi");
        if (cluttered & 1)
            emitln("pop esi");
        goto done;
    }

    res = get_reg(i);
    x86_load(res, arg1);
    pin_reg(res);
    switch (ty) {
    case TOK_SHORT:
    case TOK_UNSIGNED_SHORT:
        siz_str = "word";
        reg_str = x86_lwreg_str[res];
        break;
    case TOK_CHAR:
    case TOK_SIGNED_CHAR:
    case TOK_UNSIGNED_CHAR:
        /* TODO: handle the case where the register is esi or edi */
        siz_str = "byte";
        reg_str = x86_lbreg_str[res];
        break;
    default:
        siz_str = "dword";
        reg_str = x86_reg_str[res];
        break;
    }
    if (!addr_in_reg(tar)) {
        if ((pr=get_empty_reg()) == -1) {
            pr = get_unpinned_reg();
            assert(pr != -1);
            spill_reg(pr);
        }
        x86_load(pr, tar);
    } else {
        pr = addr_reg(tar);
    }
    emitln("mov %s [%s], %s", siz_str, x86_reg_str[pr], reg_str);
    unpin_reg(res);
done:
    update_arg_descriptors(tar, tar_liveness(i), tar_next_use(i));
    update_arg_descriptors(arg1, arg1_liveness(i), arg1_next_use(i));
}
void x86_lab(int i, unsigned tar, unsigned arg1, unsigned arg2)
{
    emit_lab(address(tar).cont.val);
}
void x86_jmp(int i, unsigned tar, unsigned arg1, unsigned arg2)
{
    emit_jmp(address(tar).cont.val);
}
void x86_arg(int i, unsigned tar, unsigned arg1, unsigned arg2)
{
    Token cat;
    Declaration ty;

    /*
     * Note: the type expression comes from the formal parameter
     * or from the actual argument, so some more care must be taken
     * when using it.
     */

    ty = *instruction(i).type;
    if (ty.idl!=NULL && ty.idl->op==TOK_ID)
        ty.idl = ty.idl->child;

    cat = get_type_category(&ty);
    if (cat!=TOK_STRUCT && cat!=TOK_UNION) {
        emitln("push %s", get_operand(arg1));
        arg_nb += 4;
    } else {
        unsigned siz, asiz;
        int cluttered, savnb;

        siz = compute_sizeof(&ty);
        asiz = round_up(siz, 4);
        emitln("sub esp, %lu", asiz);
        arg_nb += asiz;

        cluttered = savnb = 0;
        if (!addr_in_reg(arg1) || addr_reg(arg1)!=X86_ESI) {
            if (reg_descr_tab[X86_ESI].naddrs != 0) {
                cluttered |= 1;
                emitln("push esi");
                savnb += 4;
            }
            x86_load(X86_ESI, arg1);
        }
        if (reg_descr_tab[X86_EDI].naddrs != 0) {
            cluttered |= 2;
            emitln("push edi");
            savnb += 4;
        }
        if (!savnb)
            emitln("mov edi, esp");
        else
            emitln("lea edi, [esp+%d]", savnb);
        if (reg_descr_tab[X86_ECX].naddrs != 0) {
            cluttered |= 4;
            emitln("push ecx");
        }
        emitln("mov ecx, %u", siz);
        /*emitln("cld");*/
        emitln("rep movsb");
        if (cluttered & 4)
            emitln("pop ecx");
        if (cluttered & 2)
            emitln("pop edi");
        if (cluttered & 1)
            emitln("pop esi");
    }
    update_arg_descriptors(arg1, arg1_liveness(i), arg1_next_use(i));
}
void x86_ret(int i, unsigned tar, unsigned arg1, unsigned arg2)
{
    if (!big_return) {
        x86_load(X86_EAX, arg1);
    } else {
        unsigned siz;

        siz = compute_sizeof(instruction(i).type);
        /*if (reg_descr_tab[X86_ESI].naddrs != 0)
            spill_reg(X86_ESI);*/
        x86_load(X86_ESI, arg1);
        modified[X86_ESI] = TRUE;
        if (reg_descr_tab[X86_EDI].naddrs != 0)
            spill_reg(X86_EDI);
        emitln("mov edi, dword [ebp-4]");
        modified[X86_EDI] = TRUE;
        if (reg_descr_tab[X86_ECX].naddrs != 0)
            spill_reg(X86_ECX);
        emitln("mov ecx, %u", siz);
        emitln("rep movsb");
        /*if (reg_descr_tab[X86_EAX].naddrs != 0)
            spill_reg(X86_EAX);
        emitln("mov eax, dword [ebp-4]");*/
    }
    update_arg_descriptors(arg1, arg1_liveness(i), arg1_next_use(i));
}
void x86_cbr(int i, unsigned tar, unsigned arg1, unsigned arg2)
{
    compare_against_zero(tar);
    /* do any spilling before the jumps */
    update_arg_descriptors(tar, tar_liveness(i), tar_next_use(i));
    emit_jmpeq(address(arg2).cont.val);
    emit_jmp(address(arg1).cont.val);
}
void x86_nop(int i, unsigned tar, unsigned arg1, unsigned arg2)
{
    /* nothing */
}

static void (*instruction_handlers[])(int, unsigned, unsigned, unsigned) = {
    x86_add, x86_sub, x86_mul, x86_div,
    x86_rem, x86_shl, x86_shr, x86_and,
    x86_or, x86_xor, x86_eq, x86_neq,
    x86_lt, x86_let, x86_gt, x86_get,

    x86_neg, x86_cmpl, x86_not, x86_ch,
    x86_uch, x86_sh, x86_ush, x86_addr_of,
    x86_ind, x86_asn, x86_call, x86_indcall,

    x86_ind_asn, x86_lab, x86_jmp, x86_arg,
    x86_ret, x86_cbr, x86_nop
};

void x86_function_definition(TypeExp *decl_specs, TypeExp *header)
{
    /*
        cdecl calling convention
            ==> caller save: eax, ecx, edx
            ==> callee save: ebp, ebx, esi, edi

                        [ Stack frame layout ]
     => Low addresses
        +-----------------------------------------------------------+ <- ESP    EBP-?
        |               Calle save registers                        |
        +-----------------------------------------------------------+ <- EBP-?
        |               Space for temp struct/union                 |
        |   (used when calling a struct/union valued function)      |
        +-----------------------------------------------------------+
        |               ... (more vars and temps)                   |
        +-----------------------------------------------------------+ <- EBP-8
        |               First local variable                        |
        +-----------------------------------------------------------+ <- EBP-4
        |               Return Value Address                        |
        |       (used when returning a struct/union)                |
        +-----------------------------------------------------------+ <- EBP
        |               Saved EBP                                   |
        +-----------------------------------------------------------+ <- EBP+4
        |               Ret. Addr                                   |
        +-----------------------------------------------------------+ <- EBP+8
        |               First argument                              |
        +-----------------------------------------------------------+
     => High addresses
    */
    int b;
    Token cat;
    unsigned fn;
    Declaration ty;

    curr_func = header->str;
    fn = new_cg_node(curr_func);
    size_of_local_area = round_up(cg_node(fn).size_of_local_area, 4);
    init_addr_descr_tab();

    ty.decl_specs = decl_specs;
    ty.idl = header->child->child;
    big_return = ((cat=get_type_category(&ty))==TOK_STRUCT || cat==TOK_UNION);

    emit_prologln("\n; ==== start of definition of function `%s' ====", curr_func);
    emit_prologln("%s:", curr_func);
    if (big_return) {
        emit_prologln("pop eax");
        emit_prologln("xchg [esp], eax");
    }
    emit_prologln("push ebp");
    emit_prologln("mov ebp, esp");
    emit_prolog("sub esp, ");

    for (b = cg_node(fn).bb_i; b <= cg_node(fn).bb_f; b++) {
        int i;

        for (i = cfg_node(b).leader; i <= cfg_node(b).last; i++) {
            unsigned tar, arg1, arg2;

            tar = instruction(i).tar;
            arg1 = instruction(i).arg1;
            arg2 = instruction(i).arg2;

            instruction_handlers[instruction(i).op](i, tar, arg1, arg2);
        } /* end of basic block */
    }
    size_of_local_area -= temp_struct_size;
    while (--calls_to_fix_counter >= 0) {
        int n;
        char *s;

        s = calls_to_fix[calls_to_fix_counter];
        n = sprintf(s, "%d", -size_of_local_area);
        s[n++] = ']';
        for (; s[n] == 'X'; n++)
            s[n] = ' ';
    }

    emit_prologln("%d", -size_of_local_area);
    if (modified[X86_ESI]) emit_prologln("push esi");
    if (modified[X86_EDI]) emit_prologln("push edi");
    if (modified[X86_EBX]) emit_prologln("push ebx");

    if (big_return) {
        emit_prologln("mov dword [ebp-4], eax");
        emit_epilogln("mov eax, dword [ebp-4]");
    }

    if (modified[X86_EBX]) emit_epilogln("pop ebx");
    if (modified[X86_EDI]) emit_epilogln("pop edi");
    if (modified[X86_ESI]) emit_epilogln("pop esi");
    emit_epilogln("mov esp, ebp");
    emit_epilogln("pop ebp");
    emit_epilogln("ret");

    string_write(func_prolog, stdout);
    string_write(func_body, stdout);
    string_write(func_epilog, stdout);

    /* reset everything */
    string_clear(func_prolog);
    string_clear(func_body);
    string_clear(func_epilog);
    temp_struct_size = 0;
    calls_to_fix_counter = 0;
    memset(modified, 0, sizeof(int)*X86_NREG);
    free(addr_descr_tab);
    free_all_temps();
    /*memset(reg_descr_tab, 0, sizeof(RegDescr)*X86_NREG);*/
}
