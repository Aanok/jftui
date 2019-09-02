#ifndef _JF_COMMAND_PARSER
#define _JF_COMMAND_PARSER

#define YYSTYPE unsigned long
#include <stdlib.h>
#include <stdbool.h>

#include "shared.h"
#include "menu.h"

////////// STATE MACHINE //////////
typedef char jf_command_parser_state;

// make sure to start from 0 so memset init works
#define JF_CMD_VALIDATE_START	0
#define JF_CMD_VALIDATE_ATOMS	1
#define JF_CMD_VALIDATE_FOLDER	2
#define JF_CMD_VALIDATE_OK		3
#define JF_CMD_SPECIAL			4
#define JF_CMD_SUCCESS			5

#define JF_CMD_FAIL_FOLDER		-1
#define JF_CMD_FAIL_SYNTAX		-2

#define JF_CMD_IS_FAIL(state)	((state) < 0)
///////////////////////////////////


////////// YY_CTX //////////
// forward declaration wrt PEG generated code
typedef struct _yycontext yycontext;

#define YY_CTX_LOCAL
#define YY_CTX_MEMBERS				\
	jf_command_parser_state state;	\
	char *input;					\
	size_t read_input;
////////////////////////////


////////// INPUT PROCESSING //////////
#define YY_INPUT(ctx, buf, result, max_size)						\
	{																\
		size_t to_read = 0;											\
		while (to_read < max_size) {								\
			if (ctx->input[ctx->read_input + to_read] == '\0') {	\
				break;												\
			}														\
			to_read++;												\
		}															\
		memcpy(buf, ctx->input + ctx->read_input, to_read);			\
		ctx->read_input += to_read;									\
		result = to_read;											\
	}
//////////////////////////////////////


////////// FUNCTION PROTOTYPES //////////
jf_command_parser_state yy_command_parser_get_state(const yycontext *ctx);

static void yy_cmd_digest(yycontext *ctx, const size_t n);
static void yy_cmd_digest_range(yycontext *ctx, const size_t l, const size_t r);
static void yy_cmd_finalize(yycontext *ctx, const bool parse_ok);
/////////////////////////////////////////

#endif
