#include <c42.h>

typedef struct cmd_name_s cmd_name_t;
typedef struct ctx_s ctx_t;
typedef void (* cmd_func_t) (ctx_t *);

struct cmd_name_s
{
    char const * str;
    int cmd;
};

struct ctx_s
{
    c42_io8_t * in;
    c42_io8_t * out;
    c42_io8_t * err;
    c42_ma_t * ma;
    c42_fsa_t * fsa;
    uint8_t const * svc_provider;
    uint8_t const * cmd_str;
    uint8_t const * const * args;
    uint32_t argc;
    uint_fast8_t ioe;
    uint_fast8_t rc;
};

#define RC_IN (1 << 0)
#define RC_OUT (1 << 1)
#define RC_PROC (1 << 2)
#define RC_ERR (1 << 5)
#define RC_INVOKE (1 << 6)

enum cmd_enum
{
    CMD_UNKNOWN = 0,
    CMD_HELP,
    CMD_VERSION,
    CMD_ECHO,
    CMD_HEX,
    CMD_UNHEX,
    CMD_UTF8_ENCODE,
    CMD_UTF8_ENCODE_HEX,
    CMD_UCP_TERM_WIDTH,
    CMD_UTF8_ARG_TERM_WIDTH,
    CMD_CONV,
    CMD_ALLOC_TEST,
};

static cmd_name_t cmd_name_table[] =
{
    { "help"                    , CMD_HELP                  },
    { "-h"                      , CMD_HELP                  },
    { "--help"                  , CMD_HELP                  },
    { "version"                 , CMD_VERSION               },
    { "echo"                    , CMD_ECHO                  },
    { "hex"                     , CMD_HEX                   },
    { "unhex"                   , CMD_UNHEX                 },
    { "utf8-encode"             , CMD_UTF8_ENCODE           },
    { "utf8-encode-hex"         , CMD_UTF8_ENCODE_HEX       },
    { "ucp-term-width"          , CMD_UCP_TERM_WIDTH        },
    { "utf8-arg-term-width"     , CMD_UTF8_ARG_TERM_WIDTH   },
    { "conv"                    , CMD_CONV                  },
    { "alloc-test"              , CMD_ALLOC_TEST            },
    { NULL                      , CMD_UNKNOWN               }
};

/* errput *******************************************************************/
static uint_fast8_t errput (ctx_t * ctx, uint8_t const * data, size_t len)
{
    uint_fast8_t ioe;
    ioe = c42_io8_write_full(ctx->err, data, len, NULL);
    if (!ioe) return 0;
    ctx->rc |= RC_ERR;
    return ctx->rc;
}

/* errputz ******************************************************************/
static uint_fast8_t errputz (ctx_t * ctx, uint8_t const * data)
{
    return errput(ctx, data, c42_u8z_len(data));
}

/* ERRLIT *******************************************************************/
#define ERRLIT(_ctx, _lit) \
    (errput((_ctx), (uint8_t const *) (_lit), sizeof(_lit) - 1))

/* output *******************************************************************/
static uint_fast8_t output (ctx_t * ctx, uint8_t const * data, size_t len)
{
    uint_fast8_t ioe;
    ioe = c42_io8_write_full(ctx->out, data, len, NULL);
    if (!ioe) return 0;
    ctx->ioe = ioe;
    ctx->rc |= RC_OUT;
    ERRLIT(ctx, "c8: output error\n");
    return ctx->rc;
}

/* outputz ******************************************************************/
static uint_fast8_t outputz (ctx_t * ctx, uint8_t const * data)
{
    return output(ctx, data, c42_u8z_len(data));
}

/* OUTLIT *******************************************************************/
#define OUTLIT(_ctx, _lit) \
    (output((_ctx), (uint8_t const *) (_lit), sizeof(_lit) - 1))

static uint_fast8_t outfmt (ctx_t * ctx, char const * fmt, ...)
{
    uint_fast8_t e;
    va_list va;
    va_start(va, fmt);
    e = c42_io8_vfmt(ctx->out, fmt, va);
    va_end(va);
    if (!e) return 0;
    ctx->ioe = e;
    ctx->rc |= RC_OUT;
    ERRLIT(ctx, "c8: fmt output error\n");
    return ctx->rc;
}
#define O(...) if (!outfmt(ctx, __VA_ARGS__)) ; else return

/* cmd_hex ******************************************************************/
static void cmd_hex (ctx_t * ctx)
{
    uint8_t ib[0x200];
    uint8_t ob[0x400];
    size_t rsize;
    uint_fast8_t ioe, fsie;
    uint_fast8_t rc;
    uint32_t bpl, i;
    c42_io8_t * in;

    in = ctx->in;
    for (i = 0; i < ctx->argc; ++i)
    {
        if (C42_U8A_EQLIT(ctx->args[i], "if="))
        {
            if (in != ctx->in)
            {
                ctx->rc |= RC_INVOKE;
                ERRLIT(ctx, "c8: multiple 'if=' not allowed!\n");
                return;
            }
            fsie = c42_file_open(ctx->fsa, in, ctx->args[i] + 3, 
                                 C42_FSA_OPEN_EXISTING);
            if (fsie)
            {
                ctx->rc |= RC_INVOKE;
                ERRLIT(ctx, "c8: failed to open input file.\n");
                return;
            }
        }
    }

    bpl = 0x20;
    for (i = 0;;)
    {
        rsize = bpl - i;
        if (rsize > sizeof ib) rsize = sizeof ib;

        ioe = c42_io8_read(in, ib, rsize, &rsize);
        if (ioe)
        {
            if (ioe == C42_IO8_INTERRUPTED) continue;
            rc = RC_IN;
            ioe = C42_IO8_WRITE_LIT(ctx->err, "c8: input error\n");
            if (ioe) rc |= RC_ERR;
            return;
        }
        if (rsize == 0) break;
        c42_u8a_hex(ob, ib, rsize);
        if (output(ctx, ob, rsize << 1)) return;
        i += rsize;
        if (i == bpl)
        {
            i = 0;
            if (OUTLIT(ctx, "\n")) return;
        }
    }
    OUTLIT(ctx, "\n");
    return;
}

/* cmd_unhex ****************************************************************/
static void cmd_unhex (ctx_t * ctx)
{
    static uint8_t ib[0x7];
    static uint8_t ob[0x2];
    c42_io8_t * in = ctx->in;
    size_t ip, iq, iz, icz, oz;
    uint_fast8_t c, ioe;

    for (ip = 0;;)
    {
        // uint8_t y;
        // y = '@' + ip;
        // output(ctx, &y, 1);
        ioe = c42_io8_read(in, &ib[ip], sizeof(ib) - ip, &iz);
        if (ioe == C42_IO8_INTERRUPTED) continue;
        if (iz == 0) break;

        iz += ip;
        for (ip = 0; ip < iz; )
        {
            // uint8_t x;
            // OUTLIT(ctx, "{");
            // if (output(ctx, &ib[ip], iz - ip)) return;
            // OUTLIT(ctx, "}");
            c = c42_clconv_hex_to_bin(&ib[ip], iz - ip, &icz, 
                                      ob, sizeof(ob), &oz, 
                                      " \n\r\t");
            // OUTLIT(ctx, "[");
            // x = '0' + c; output(ctx, &x, 1);
            // x = '0' + icz; output(ctx, &x, 1);
            // OUTLIT(ctx, "[");
            if (oz && output(ctx, ob, oz)) return;
            // OUTLIT(ctx, "]");
            // OUTLIT(ctx, "]");
            if (c == C42_CLCONV_MALFORMED)
            {
                ctx->rc |= RC_PROC;
                ERRLIT(ctx, "c8: malformed input\n");
                return;
            }
            ip += icz;
            if (c == C42_CLCONV_INCOMPLETE && oz == 0) break;
        }
        // OUTLIT(ctx, "<");
        for (iq = ip, ip = 0; iq < iz; ip++, iq++)
            ib[ip] = ib[iq];
        // output(ctx, ib, ip);
        // OUTLIT(ctx, ">");
    }
    if (ip)
    {
        ctx->rc |= RC_PROC;
        ERRLIT(ctx, "c8: unterminated input\n");
    }
}

/* cmd_unknown **************************************************************/
static void cmd_unknown (ctx_t * ctx)
{
    ctx->rc |= RC_INVOKE;
    if (ERRLIT(ctx, "c8: unknown command \"")
        || errputz(ctx, ctx->cmd_str)
        || ERRLIT(ctx, "\"\n")) 
        return;
}

/* cmd_help *****************************************************************/
static void cmd_help (ctx_t * ctx)
{
    OUTLIT(ctx, 
 "c8 - compilation of portable tools\n"
 "Usage: c8 CMD [ARGS]\n"
 "Commands:\n"
 "  help                            prints this text\n"
 "  version                         prints program version using\n"
 "                                  c8-vHEX C42_LIB_NAME C42CLIA_NAME\n"
 "  echo ARGS                       prints given args, one per line\n"
 "  hex                             reads from standard input and writes\n"
 "                                  the bytes in hex form to std output\n"
 "  utf8-encode INT_LIST            converts given ints into UTF8\n"
 "  utf8-encode-hex INT_LIST        converts given ints into UTF8 and prints\n"
 "                                  each sequence in hex, separating them\n"
 "                                  with spaces\n"
 "  ucp-term-width INT_LIST         prints the terminal width of each Unicode\n"
 "                                  codepoint given as an integer;\n"
 "  utf8-arg-term-width STRINGS     prints the terminal width of each argument;\n"
 "                                  uses -1 for args with non-printable chars\n"
 "                                  uses -2 for bad UTF-8 args\n"
 "  conv CONVERTER                  converts standard input to standard output\n"
 "                                  using the specified converter:\n"
 "                                  unhex - reads hex data ignoring whitespace\n"
 "                                          and produces binary data\n"
 "  alloc-test SIZE                 allocates and fills a buffer of SIZE bytes\n"
 );
}

/* cmd_version **************************************************************/
static void cmd_version (ctx_t * ctx)
{
    if (OUTLIT(ctx, "c8-v0000 ") 
        || outputz(ctx, c42_lib_name())
        || OUTLIT(ctx, " ")
        || outputz(ctx, ctx->svc_provider)
        || OUTLIT(ctx, "\n"))
        return;
}

/* cmd_echo *****************************************************************/
static void cmd_echo (ctx_t * ctx)
{
    uint32_t i;
    for (i = 0; i < ctx->argc; ++i)
        if (outputz(ctx, ctx->args[i]) || OUTLIT(ctx, "\n")) return;
}

/* cmd_utf8_encode **********************************************************/
static void cmd_utf8_encode (ctx_t * ctx)
{
    uint32_t i;

    for (i = 0; i < ctx->argc; ++i)
    {
        size_t alen, ilen;
        uint64_t u64;
        uint32_t ucp;
        uint8_t utf8_buf[0x4];
        uint_fast8_t r;

        alen = c42_u8z_len(ctx->args[i]);
        r = c42_u64_from_str(ctx->args[i], alen, 0, &u64, NULL);
        if (r)
        {
            ctx->rc |= RC_INVOKE;
            ERRLIT(ctx, "c8: cannot convert to integer\n");
            return;
        }
        ucp = (uint32_t) u64;
        if ((uint64_t) ucp != u64 || !c42_ucp_is_valid(ucp))
        {
            ctx->rc |= RC_PROC;
            ERRLIT(ctx, "c8: invalid unicode codepoint\n");
            return;
        }

        ilen = c42_ucp_to_utf8(utf8_buf, ucp);
        if (output(ctx, utf8_buf, ilen)) return;
    }
    OUTLIT(ctx, "\n");
}

/* cmd_utf8_encode_hex ******************************************************/
static void cmd_utf8_encode_hex (ctx_t * ctx)
{
    uint32_t i;

    for (i = 0; i < ctx->argc; ++i)
    {
        size_t alen, ilen;
        uint64_t u64;
        uint32_t ucp;
        uint8_t utf8_buf[0x4];
        uint8_t hex_buf[8];
        uint_fast8_t r;

        alen = c42_u8z_len(ctx->args[i]);
        r = c42_u64_from_str(ctx->args[i], alen, 0, &u64, NULL);
        if (r)
        {
            ctx->rc |= RC_INVOKE;
            ERRLIT(ctx, "c8: cannot convert to integer\n");
            return;
        }
        ucp = (uint32_t) u64;
        if ((uint64_t) ucp != u64 || !c42_ucp_is_valid(ucp))
        {
            ctx->rc |= RC_PROC;
            ERRLIT(ctx, "c8: invalid unicode codepoint\n");
            return;
        }

        ilen = c42_ucp_to_utf8(utf8_buf, ucp);
        c42_u8a_hex(hex_buf, utf8_buf, ilen);
        if (output(ctx, hex_buf, ilen << 1)) return;
        // ilen = c42_u64_to_str(buf, u64, 16, 7, 4, '_');
        // if (output(ctx, buf, ilen)) return;
        if (i < ctx->argc - 1) { if (OUTLIT(ctx, " ")) return; }
    }
    OUTLIT(ctx, "\n");
}

/* cmd_ucp_term_width *******************************************************/
static void cmd_ucp_term_width (ctx_t * ctx)
{
    uint32_t i;

    for (i = 0; i < ctx->argc; ++i)
    {
        size_t alen;
        uint64_t u64;
        uint32_t ucp;
        uint_fast8_t r;

        alen = c42_u8z_len(ctx->args[i]);
        r = c42_u64_from_str(ctx->args[i], alen, 0, &u64, NULL);
        if (r)
        {
            ctx->rc |= RC_INVOKE;
            ERRLIT(ctx, "c8: cannot convert to integer\n");
            return;
        }
        ucp = (uint32_t) u64;
        if ((uint64_t) ucp != u64 || !c42_ucp_is_valid(ucp))
        {
            ctx->rc |= RC_PROC;
            ERRLIT(ctx, "c8: invalid unicode codepoint\n");
            return;
        }

        if (!c42_ucp_is_valid(ucp))
        {
            if (OUTLIT(ctx, "-2")) return;
        }
        else
        {
            int w = c42_ucp_term_width(ucp);
            if (w < 0)
            {
                if (OUTLIT(ctx, "-1")) return;
            }
            else
            {
                uint8_t d = '0' + w;
                if (output(ctx, &d, 1)) return;
            }

        }
        if (i < ctx->argc - 1) { if (OUTLIT(ctx, " ")) return; }
    }
    OUTLIT(ctx, "\n");
}

/* cmd_utf8_encode_hex ******************************************************/
static void cmd_utf8_arg_term_width (ctx_t * ctx)
{
    uint32_t i;
    uint_fast8_t vs;
    for (i = 0; i < ctx->argc; ++i)
    {
        size_t blen, alen, pos;
        int32_t w;
        uint8_t buf[0x10];

        if (i) { if (OUTLIT(ctx, " ")) return; }
        alen = c42_u8z_len(ctx->args[i]);
        vs = c42_utf8_validate(ctx->args[i], alen, &pos);
        if (vs)
        {
            ctx->rc |= RC_PROC;
            ERRLIT(ctx, "c8: invalid UTF-8 argument\n");
            return;
        }
        w = c42_utf8_term_width(ctx->args[i], alen, NULL);
        if (w < 0)
        {
            ctx->rc |= RC_PROC;
            if (w == -1) ERRLIT(ctx, "c8: non-printable codepoint\n");
            else ERRLIT(ctx, "c8: width too large\n");
            return;
        }
        blen = c42_u64_to_str(buf, w, 10, 0, 64, 0);
        if (output(ctx, buf, blen)) return;
    }
    OUTLIT(ctx, "\n");
}

/* cmd_conv *****************************************************************/
static void cmd_conv (ctx_t * ctx)
{
    static uint8_t ib[0x800];
    static uint8_t ob[0x800];
    c42_io8_t * in = ctx->in;
    size_t ip, iq, iz, icz, oz;
    uint_fast8_t c, ioe;
    c42_clconv_f conv;
    void * conv_ctx;

    if (ctx->argc != 1)
    {
        ctx->rc |= RC_INVOKE;
        ERRLIT(ctx, "c8: missing converter name argument\n");
        return;
    }

    if (C42_U8Z_EQLIT(ctx->args[0], "unhex"))
    {
        conv = &c42_clconv_hex_to_bin;
        conv_ctx = " \n\r\t";
    }
    else if (C42_U8Z_EQLIT(ctx->args[0], "hex"))
    {
        conv = &c42_clconv_bin_to_hex_line;
        conv_ctx = NULL;
    }
    else
    {
        ctx->rc |= RC_INVOKE;
        ERRLIT(ctx, "c8: unrecognised converter name\n");
        return;
    }

    for (ip = 0;;)
    {
        ioe = c42_io8_read(in, &ib[ip], sizeof(ib) - ip, &iz);
        if (ioe == C42_IO8_INTERRUPTED) continue;
        if (iz == 0) break;

        iz += ip;
        for (ip = 0; ip < iz; )
        {
            c = conv(&ib[ip], iz - ip, &icz, ob, sizeof(ob), &oz, conv_ctx);
            if (oz && output(ctx, ob, oz)) return;
            if (c == C42_CLCONV_MALFORMED)
            {
                ctx->rc |= RC_PROC;
                ERRLIT(ctx, "c8: malformed input\n");
                return;
            }
            ip += icz;
            if (c == C42_CLCONV_INCOMPLETE && oz == 0) break;
        }
        for (iq = ip, ip = 0; iq < iz; ip++, iq++)
            ib[ip] = ib[iq];
    }
    if (ip)
    {
        ctx->rc |= RC_PROC;
        ERRLIT(ctx, "c8: unterminated input\n");
    }
}

/* cmd_alloc_test ***********************************************************/
static void cmd_alloc_test (ctx_t * ctx)
{
    uint64_t size;
    size_t i, z;
    size_t alen;
    uint_fast8_t r;
    uint8_t * a;

    if (ctx->argc != 1)
    {
        ctx->rc |= RC_INVOKE;
        ERRLIT(ctx, "c8 alloc-test: missing SIZE argument\n");
        return;
    }

    alen = c42_u8z_len(ctx->args[0]);
    r = c42_u64_from_str(ctx->args[0], alen, 0, &size, NULL);
    if (r)
    {
        ctx->rc |= RC_INVOKE;
        ERRLIT(ctx, 
               "c8 alloc-test: cannot convert given argument to integer\n");
        return;
    }
    if (size > PTRDIFF_MAX)
    {
        ctx->rc |= RC_PROC;
        ERRLIT(ctx, "c8 alloc-test: given size is too large\n");
        return;
    }
    r = c42_ma_alloc(ctx->ma, (void * *) &a, sizeof(uint8_t), size);
    if (r)
    {
        ctx->rc |= RC_PROC;
        ERRLIT(ctx, "c8 alloc-test: alloc failed!\n");
        return;
    }
    O("ptr: $xp\n", a);
    for (i = 0, z = size; i < z; ++i) a[i] = (uint8_t) i;
}

/* cmd_func_table ***********************************************************/
static cmd_func_t cmd_func_table[] =
{
    &cmd_unknown,
    &cmd_help,
    &cmd_version,
    &cmd_echo,
    &cmd_hex,
    &cmd_unhex,
    &cmd_utf8_encode,
    &cmd_utf8_encode_hex,
    &cmd_ucp_term_width,
    &cmd_utf8_arg_term_width,
    &cmd_conv,
    &cmd_alloc_test,
};

/* c42_main *****************************************************************/
uint_fast8_t C42_CALL c42_main
(
    c42_svc_t * svc,
    c42_clia_t * clia
)
{
    size_t i;
    int cmd = CMD_UNKNOWN;
    ctx_t ctx;

    ctx.svc_provider = svc->provider;
    /* determine command */
    if (clia->argc == 0) 
    {
        cmd = CMD_HELP;
        ctx.cmd_str = (uint8_t const *) "help";
        ctx.argc = 0;
    }
    else
    {
        ctx.cmd_str = clia->args[0];
        for (i = 0; cmd_name_table[i].cmd; ++i)
            if (C42_U8Z_EQUAL(clia->args[0], 
                              (uint8_t const *) cmd_name_table[i].str))
            { 
                cmd = cmd_name_table[i].cmd; 
                ctx.args = clia->args + 1;
                ctx.argc = clia->argc - 1;
                break; 
            }
        if (cmd == CMD_UNKNOWN) ctx.argc = 0;
    }

    /* prepare context */
    ctx.in = &clia->stdio.in;
    ctx.out = &clia->stdio.out;
    ctx.err = &clia->stdio.err;
    ctx.ma = &svc->ma;
    ctx.fsa = &svc->fsa;
    ctx.rc = 0;

    cmd_func_table[cmd](&ctx);

    return ctx.rc;
}

