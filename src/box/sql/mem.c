/*
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "sqlInt.h"
#include "mem.h"
#include "vdbeInt.h"
#include "coll/coll.h"
#include "tarantoolInt.h"
#include "box/schema.h"
#include "box/tuple.h"
#include "mpstream/mpstream.h"

bool
mem_is_null(const struct Mem *mem)
{
	return (mem->flags & MEM_Null) != 0;
}

bool
mem_is_unsigned(const struct Mem *mem)
{
	return (mem->flags & MEM_UInt) != 0;
}

bool
mem_is_string(const struct Mem *mem)
{
	return (mem->flags & MEM_Str) != 0;
}

bool
mem_is_number(const struct Mem *mem)
{
	return (mem->flags & (MEM_Real | MEM_Int |MEM_UInt)) != 0;
}

bool
mem_is_double(const struct Mem *mem)
{
	return (mem->flags & MEM_Real) != 0;
}

bool
mem_is_integer(const struct Mem *mem)
{
	return (mem->flags & (MEM_Int | MEM_UInt)) != 0;
}

bool
mem_is_boolean(const struct Mem *mem)
{
	return (mem->flags & MEM_Bool) != 0;
}

bool
mem_is_binary(const struct Mem *mem)
{
	return (mem->flags & MEM_Blob) != 0;
}

bool
mem_is_map(const struct Mem *mem)
{
	return (mem->flags & MEM_Blob) != 0 &&
	       (mem->flags & MEM_Subtype) != 0 &&
	       mem->subtype == SQL_SUBTYPE_MSGPACK &&
	       mp_typeof(*mem->z) == MP_MAP;
}

bool
mem_is_array(const struct Mem *mem)
{
	return (mem->flags & MEM_Blob) != 0 &&
	       (mem->flags & MEM_Subtype) != 0 &&
	       mem->subtype == SQL_SUBTYPE_MSGPACK &&
	       mp_typeof(*mem->z) == MP_ARRAY;
}

bool
mem_is_aggregate(const struct Mem *mem)
{
	return (mem->flags & MEM_Agg) != 0;
}

bool
mem_is_varstring(const struct Mem *mem)
{
	return (mem->flags & (MEM_Blob | MEM_Str)) != 0;
}

bool
mem_is_frame(const struct Mem *mem)
{
	return (mem->flags & MEM_Frame) != 0;
}

bool
mem_is_undefined(const struct Mem *mem)
{
	return (mem->flags & MEM_Undefined) != 0;
}

bool
mem_is_static(const struct Mem *mem)
{
	return (mem->flags & (MEM_Str | MEM_Blob)) != 0 &&
	       (mem->flags & MEM_Static) != 0;
}

bool
mem_is_ephemeral(const struct Mem *mem)
{
	return (mem->flags & (MEM_Str | MEM_Blob)) != 0 &&
	       (mem->flags & MEM_Ephem) != 0;
}

bool
mem_is_dynamic(const struct Mem *mem)
{
	return (mem->flags & (MEM_Str | MEM_Blob)) != 0 &&
	       (mem->flags & MEM_Dyn) != 0;
}

bool
mem_is_allocated(const struct Mem *mem)
{
	return (mem->flags & MEM_Blob) != 0 && mem->z == mem->zMalloc;
}

bool
mem_is_cleared(const struct Mem *mem)
{
	return (mem->flags & MEM_Null) != 0 && (mem->flags & MEM_Cleared) != 0;
}

bool
mem_is_zeroblob(const struct Mem *mem)
{
	return (mem->flags & MEM_Blob) != 0 && (mem->flags & MEM_Zero) != 0;
}

bool
mem_is_same_type(const struct Mem *mem1, const struct Mem *mem2)
{
	return (mem1->flags & MEM_PURE_TYPE_MASK) ==
	       (mem2->flags & MEM_PURE_TYPE_MASK);
}

const char *
mem_str(const struct Mem *mem)
{
	switch (mem->flags & MEM_PURE_TYPE_MASK) {
	case MEM_Null:
		return "NULL";
	case MEM_Str:
		return tt_sprintf("%.*s", mem->n, mem->z);
	case MEM_Int:
		return tt_sprintf("%lld", mem->u.i);
	case MEM_UInt:
		return tt_sprintf("%llu", mem->u.u);
	case MEM_Real:
		return tt_sprintf("%.16g", mem->u.r);
	case MEM_Blob:
		if ((mem->flags & MEM_Subtype) != 0 &&
		    mem->subtype == SQL_SUBTYPE_MSGPACK)
			return mp_str(mem->z);
		return "varbinary";
	case MEM_Bool:
		return mem->u.b ? "TRUE" : "FALSE";
	default:
		break;
	}
	return "unknown";
}

void
mem_create(struct Mem *mem)
{
	mem->flags = MEM_Null;
	mem->subtype = SQL_SUBTYPE_NO;
	mem->field_type = field_type_MAX;
	mem->n = 0;
	mem->z = NULL;
	mem->zMalloc = NULL;
	mem->szMalloc = 0;
	mem->uTemp = 0;
	mem->db = sql_get();
	mem->xDel = NULL;
#ifdef SQL_DEBUG
	mem->pScopyFrom = NULL;
	mem->pFiller = NULL;
#endif
}

static void
mem_clear(struct Mem *mem)
{
	assert(VdbeMemDynamic(mem));
	if ((mem->flags & MEM_Agg) != 0)
		sql_vdbemem_finalize(mem, mem->u.func);
	assert((mem->flags & MEM_Agg) == 0);
	if ((mem->flags & MEM_Dyn) != 0) {
		assert(mem->xDel != SQL_DYNAMIC && mem->xDel != NULL);
		mem->xDel((void *)mem->z);
	} else if ((mem->flags & MEM_Frame) != 0) {
		struct VdbeFrame *frame = mem->u.pFrame;
		frame->pParent = frame->v->pDelFrame;
		frame->v->pDelFrame = frame;
	}
	mem->flags = MEM_Null;
}

void
mem_destroy(struct Mem *mem)
{
	if (VdbeMemDynamic(mem))
		mem_clear(mem);
	if (mem->szMalloc > 0)
		sqlDbFree(mem->db, mem->zMalloc);
	mem->n = 0;
	mem->z = NULL;
	mem->szMalloc = 0;
	mem->zMalloc = NULL;
}

int
mem_copy(struct Mem *to, const struct Mem *from)
{
	if (VdbeMemDynamic(to))
		mem_clear(to);
	memcpy(to, from, MEMCELLSIZE);
	if ((to->flags & (MEM_Str | MEM_Blob)) == 0)
		return 0;
	if ((to->flags & MEM_Static) != 0)
		return 0;
	if ((to->flags & (MEM_Zero | MEM_Blob)) == (MEM_Zero | MEM_Blob)) {
		if (sqlVdbeMemExpandBlob(to) != 0)
			return -1;
		return 0;
	}
	if (to->szMalloc == 0)
		to->zMalloc = sqlDbMallocRaw(to->db, to->n);
	else
		to->zMalloc = sqlDbReallocOrFree(to->db, to->zMalloc, to->n);
	if (to->zMalloc == NULL)
		return -1;
	to->szMalloc = sqlDbMallocSize(to->db, to->zMalloc);
	memcpy(to->zMalloc, to->z, to->n);
	to->z = to->zMalloc;
	to->flags &= (MEM_Str | MEM_Blob | MEM_Term | MEM_Subtype);
	return 0;
}

int
mem_copy_as_ephemeral(struct Mem *to, const struct Mem *from)
{
	if (VdbeMemDynamic(to))
		mem_clear(to);
	memcpy(to, from, MEMCELLSIZE);
	if ((to->flags & (MEM_Str | MEM_Blob)) == 0)
		return 0;
	if ((to->flags & (MEM_Static | MEM_Ephem)) != 0)
		return 0;
	to->flags &= (MEM_Str | MEM_Blob | MEM_Term | MEM_Zero | MEM_Subtype);
	to->flags |= MEM_Ephem;
	return 0;
}

int
mem_move(struct Mem *to, struct Mem *from)
{
	mem_destroy(to);
	memcpy(to, from, sizeof(*to));
	from->flags = MEM_Null;
	from->szMalloc = 0;
	from->zMalloc = NULL;
	return 0;
}

static inline bool
mem_has_msgpack_subtype(struct Mem *mem)
{
	return (mem->flags & MEM_Subtype) != 0 &&
	       mem->subtype == SQL_SUBTYPE_MSGPACK;
}

/*
 * The pVal argument is known to be a value other than NULL.
 * Convert it into a string with encoding enc and return a pointer
 * to a zero-terminated version of that string.
 */
static SQL_NOINLINE const void *
valueToText(sql_value * pVal)
{
	assert(pVal != 0);
	assert((pVal->flags & (MEM_Null)) == 0);
	if ((pVal->flags & (MEM_Blob | MEM_Str)) &&
	    !mem_has_msgpack_subtype(pVal)) {
		if (ExpandBlob(pVal))
			return 0;
		pVal->flags |= MEM_Str;
		sqlVdbeMemNulTerminate(pVal);	/* IMP: R-31275-44060 */
	} else {
		sqlVdbeMemStringify(pVal);
		assert(0 == (1 & SQL_PTR_TO_INT(pVal->z)));
	}
	return pVal->z;
}

/**
 * According to ANSI SQL string value can be converted to boolean
 * type if string consists of literal "true" or "false" and
 * number of leading and trailing spaces.
 *
 * For instance, "   tRuE  " can be successfully converted to
 * boolean value true.
 *
 * @param str String to be converted to boolean. Assumed to be
 *        null terminated.
 * @param[out] result Resulting value of cast.
 * @retval 0 If string satisfies conditions above.
 * @retval -1 Otherwise.
 */
static int
str_cast_to_boolean(const char *str, bool *result)
{
	assert(str != NULL);
	for (; *str == ' '; str++);
	if (strncasecmp(str, SQL_TOKEN_TRUE, strlen(SQL_TOKEN_TRUE)) == 0) {
		*result = true;
		str += 4;
	} else if (strncasecmp(str, SQL_TOKEN_FALSE,
			       strlen(SQL_TOKEN_FALSE)) == 0) {
		*result = false;
		str += 5;
	} else {
		return -1;
	}
	for (; *str != '\0'; ++str) {
		if (*str != ' ')
			return -1;
	}
	return 0;
}

/*
 * Convert a 64-bit IEEE double into a 64-bit signed integer.
 * If the double is out of range of a 64-bit signed integer then
 * return the closest available 64-bit signed integer.
 */
static int
doubleToInt64(double r, int64_t *i)
{
	/*
	 * Many compilers we encounter do not define constants for the
	 * minimum and maximum 64-bit integers, or they define them
	 * inconsistently.  And many do not understand the "LL" notation.
	 * So we define our own static constants here using nothing
	 * larger than a 32-bit integer constant.
	 */
	static const int64_t maxInt = LARGEST_INT64;
	static const int64_t minInt = SMALLEST_INT64;
	if (r <= (double)minInt) {
		*i = minInt;
		return -1;
	} else if (r >= (double)maxInt) {
		*i = maxInt;
		return -1;
	} else {
		*i = (int64_t) r;
		return *i != r;
	}
}

/*
 * It is already known that pMem contains an unterminated string.
 * Add the zero terminator.
 */
static SQL_NOINLINE int
vdbeMemAddTerminator(Mem * pMem)
{
	if (sqlVdbeMemGrow(pMem, pMem->n + 2, 1)) {
		return -1;
	}
	pMem->z[pMem->n] = 0;
	pMem->z[pMem->n + 1] = 0;
	pMem->flags |= MEM_Term;
	return 0;
}

/*
 * Both *pMem1 and *pMem2 contain string values. Compare the two values
 * using the collation sequence pColl. As usual, return a negative , zero
 * or positive value if *pMem1 is less than, equal to or greater than
 * *pMem2, respectively. Similar in spirit to "rc = (*pMem1) - (*pMem2);".
 *
 * Strungs assume to be UTF-8 encoded
 */
static int
vdbeCompareMemString(const Mem * pMem1, const Mem * pMem2,
		     const struct coll * pColl)
{
	return pColl->cmp(pMem1->z, (size_t)pMem1->n,
			      pMem2->z, (size_t)pMem2->n, pColl);
}

/*
 * The input pBlob is guaranteed to be a Blob that is not marked
 * with MEM_Zero.  Return true if it could be a zero-blob.
 */
static int
isAllZero(const char *z, int n)
{
	int i;
	for (i = 0; i < n; i++) {
		if (z[i])
			return 0;
	}
	return 1;
}

char *
mem_type_to_str(const struct Mem *p)
{
	assert(p != NULL);
	switch (p->flags & MEM_PURE_TYPE_MASK) {
	case MEM_Null:
		return "NULL";
	case MEM_Str:
		return "text";
	case MEM_Int:
		return "integer";
	case MEM_UInt:
		return "unsigned";
	case MEM_Real:
		return "real";
	case MEM_Blob:
		return "varbinary";
	case MEM_Bool:
		return "boolean";
	default:
		unreachable();
	}
}

enum mp_type
mem_mp_type(struct Mem *mem)
{
	switch (mem->flags & MEM_PURE_TYPE_MASK) {
	case MEM_Int:
		return MP_INT;
	case MEM_UInt:
		return MP_UINT;
	case MEM_Real:
		return MP_DOUBLE;
	case MEM_Str:
		return MP_STR;
	case MEM_Blob:
		if ((mem->flags & MEM_Subtype) == 0 ||
		     mem->subtype != SQL_SUBTYPE_MSGPACK)
			return MP_BIN;
		assert(mp_typeof(*mem->z) == MP_MAP ||
		       mp_typeof(*mem->z) == MP_ARRAY);
		return mp_typeof(*mem->z);
	case MEM_Bool:
		return MP_BOOL;
	case MEM_Null:
		return MP_NIL;
	default: unreachable();
	}
}

/* EVIDENCE-OF: R-12793-43283 Every value in sql has one of five
 * fundamental datatypes: 64-bit signed integer 64-bit IEEE floating
 * point number string BLOB NULL
 */
enum mp_type
sql_value_type(sql_value *pVal)
{
	struct Mem *mem = (struct Mem *) pVal;
	return mem_mp_type(mem);
}


/*
 * pMem currently only holds a string type (or maybe a BLOB that we can
 * interpret as a string if we want to).  Compute its corresponding
 * numeric type, if has one.  Set the pMem->u.r and pMem->u.i fields
 * accordingly.
 */
static u16 SQL_NOINLINE
computeNumericType(Mem *pMem)
{
	assert((pMem->flags & (MEM_Int | MEM_UInt | MEM_Real)) == 0);
	assert((pMem->flags & (MEM_Str|MEM_Blob))!=0);
	if (sqlAtoF(pMem->z, &pMem->u.r, pMem->n)==0)
		return 0;
	bool is_neg;
	if (sql_atoi64(pMem->z, (int64_t *) &pMem->u.i, &is_neg, pMem->n) == 0)
		return is_neg ? MEM_Int : MEM_UInt;
	return MEM_Real;
}

/*
 * Return the numeric type for pMem, either MEM_Int or MEM_Real or both or
 * none.
 *
 * Unlike mem_apply_numeric_type(), this routine does not modify pMem->flags.
 * But it does set pMem->u.r and pMem->u.i appropriately.
 */
u16
numericType(Mem *pMem)
{
	if ((pMem->flags & (MEM_Int | MEM_UInt | MEM_Real)) != 0)
		return pMem->flags & (MEM_Int | MEM_UInt | MEM_Real);
	if (pMem->flags & (MEM_Str|MEM_Blob)) {
		return computeNumericType(pMem);
	}
	return 0;
}

/*
 * The sqlValueBytes() routine returns the number of bytes in the
 * sql_value object assuming that it uses the encoding "enc".
 * The valueBytes() routine is a helper function.
 */
static SQL_NOINLINE int
valueBytes(sql_value * pVal)
{
	return valueToText(pVal) != 0 ? pVal->n : 0;
}

int
sqlValueBytes(sql_value * pVal)
{
	Mem *p = (Mem *) pVal;
	assert((p->flags & MEM_Null) == 0
	       || (p->flags & (MEM_Str | MEM_Blob)) == 0);
	if ((p->flags & MEM_Str) != 0) {
		return p->n;
	}
	if ((p->flags & MEM_Blob) != 0) {
		if (p->flags & MEM_Zero) {
			return p->n + p->u.nZero;
		} else {
			return p->n;
		}
	}
	if (p->flags & MEM_Null)
		return 0;
	return valueBytes(pVal);
}


#ifdef SQL_DEBUG
/*
 * This routine prepares a memory cell for modification by breaking
 * its link to a shallow copy and by marking any current shallow
 * copies of this cell as invalid.
 *
 * This is used for testing and debugging only - to make sure shallow
 * copies are not misused.
 */
void
sqlVdbeMemAboutToChange(Vdbe * pVdbe, Mem * pMem)
{
	int i;
	Mem *pX;
	for (i = 0, pX = pVdbe->aMem; i < pVdbe->nMem; i++, pX++) {
		if ((pX->flags & (MEM_Blob | MEM_Str)) != 0 &&
		    (pX->flags & (MEM_Ephem | MEM_Static)) == 0) {
			if (pX->pScopyFrom == pMem) {
				pX->flags |= MEM_Undefined;
				pX->pScopyFrom = 0;
			}
		}
	}
	pMem->pScopyFrom = 0;
}

/*
 * Check invariants on a Mem object.
 *
 * This routine is intended for use inside of assert() statements, like
 * this:    assert( sqlVdbeCheckMemInvariants(pMem) );
 */
int
sqlVdbeCheckMemInvariants(Mem * p)
{
	/* If MEM_Dyn is set then Mem.xDel!=0.
	 * Mem.xDel is might not be initialized if MEM_Dyn is clear.
	 */
	assert((p->flags & MEM_Dyn) == 0 || p->xDel != 0);

	/* MEM_Dyn may only be set if Mem.szMalloc==0.  In this way we
	 * ensure that if Mem.szMalloc>0 then it is safe to do
	 * Mem.z = Mem.zMalloc without having to check Mem.flags&MEM_Dyn.
	 * That saves a few cycles in inner loops.
	 */
	assert((p->flags & MEM_Dyn) == 0 || p->szMalloc == 0);

	/* Cannot be both MEM_Int and MEM_Real at the same time */
	assert((p->flags & (MEM_Int | MEM_Real)) != (MEM_Int | MEM_Real));
	/* Can't be both UInt and Int at the same time.  */
	assert((p->flags & (MEM_Int | MEM_UInt)) != (MEM_Int | MEM_UInt));

	/* The szMalloc field holds the correct memory allocation size */
	assert(p->szMalloc == 0 ||
	       p->szMalloc == sqlDbMallocSize(p->db, p->zMalloc));

	/* If p holds a string or blob, the Mem.z must point to exactly
	 * one of the following:
	 *
	 *   (1) Memory in Mem.zMalloc and managed by the Mem object
	 *   (2) Memory to be freed using Mem.xDel
	 *   (3) An ephemeral string or blob
	 *   (4) A static string or blob
	 */
	if ((p->flags & (MEM_Str | MEM_Blob)) && p->n > 0) {
		assert(((p->szMalloc > 0 && p->z == p->zMalloc) ? 1 : 0) +
		       ((p->flags & MEM_Dyn) != 0 ? 1 : 0) +
		       ((p->flags & MEM_Ephem) != 0 ? 1 : 0) +
		       ((p->flags & MEM_Static) != 0 ? 1 : 0) == 1);
	}
	return 1;
}

/*
 * Print the SQL that was used to generate a VDBE program.
 */
void
sqlVdbePrintSql(Vdbe * p)
{
	const char *z = 0;
	if (p->zSql) {
		z = p->zSql;
	} else if (p->nOp >= 1) {
		const VdbeOp *pOp = &p->aOp[0];
		if (pOp->opcode == OP_Init && pOp->p4.z != 0) {
			z = pOp->p4.z;
			while (sqlIsspace(*z))
				z++;
		}
	}
	if (z)
		printf("SQL: [%s]\n", z);
}

/*
 * Write a nice string representation of the contents of cell pMem
 * into buffer zBuf, length nBuf.
 */
void
sqlVdbeMemPrettyPrint(Mem *pMem, char *zBuf)
{
	char *zCsr = zBuf;
	int f = pMem->flags;

	if (f&MEM_Blob) {
		int i;
		char c;
		if (f & MEM_Dyn) {
			c = 'z';
			assert((f & (MEM_Static|MEM_Ephem))==0);
		} else if (f & MEM_Static) {
			c = 't';
			assert((f & (MEM_Dyn|MEM_Ephem))==0);
		} else if (f & MEM_Ephem) {
			c = 'e';
			assert((f & (MEM_Static|MEM_Dyn))==0);
		} else {
			c = 's';
		}

		sql_snprintf(100, zCsr, "%c", c);
		zCsr += sqlStrlen30(zCsr);
		sql_snprintf(100, zCsr, "%d[", pMem->n);
		zCsr += sqlStrlen30(zCsr);
		for(i=0; i<16 && i<pMem->n; i++) {
			sql_snprintf(100, zCsr, "%02X", ((int)pMem->z[i] & 0xFF));
			zCsr += sqlStrlen30(zCsr);
		}
		for(i=0; i<16 && i<pMem->n; i++) {
			char z = pMem->z[i];
			if (z<32 || z>126) *zCsr++ = '.';
			else *zCsr++ = z;
		}
		sql_snprintf(100, zCsr, "]%s", "(8)");
		zCsr += sqlStrlen30(zCsr);
		if (f & MEM_Zero) {
			sql_snprintf(100, zCsr,"+%dz",pMem->u.nZero);
			zCsr += sqlStrlen30(zCsr);
		}
		*zCsr = '\0';
	} else if (f & MEM_Str) {
		int j, k;
		zBuf[0] = ' ';
		if (f & MEM_Dyn) {
			zBuf[1] = 'z';
			assert((f & (MEM_Static|MEM_Ephem))==0);
		} else if (f & MEM_Static) {
			zBuf[1] = 't';
			assert((f & (MEM_Dyn|MEM_Ephem))==0);
		} else if (f & MEM_Ephem) {
			zBuf[1] = 'e';
			assert((f & (MEM_Static|MEM_Dyn))==0);
		} else {
			zBuf[1] = 's';
		}
		k = 2;
		sql_snprintf(100, &zBuf[k], "%d", pMem->n);
		k += sqlStrlen30(&zBuf[k]);
		zBuf[k++] = '[';
		for(j=0; j<15 && j<pMem->n; j++) {
			u8 c = pMem->z[j];
			if (c>=0x20 && c<0x7f) {
				zBuf[k++] = c;
			} else {
				zBuf[k++] = '.';
			}
		}
		zBuf[k++] = ']';
		sql_snprintf(100,&zBuf[k],"(8)");
		k += sqlStrlen30(&zBuf[k]);
		zBuf[k++] = 0;
	}
}

/*
 * Print the value of a register for tracing purposes:
 */
static void
memTracePrint(Mem *p)
{
	if (p->flags & MEM_Undefined) {
		printf(" undefined");
	} else if (p->flags & MEM_Null) {
		printf(" NULL");
	} else if ((p->flags & (MEM_Int|MEM_Str))==(MEM_Int|MEM_Str)) {
		printf(" si:%lld", p->u.i);
	} else if (p->flags & MEM_Int) {
		printf(" i:%lld", p->u.i);
	} else if (p->flags & MEM_UInt) {
		printf(" u:%"PRIu64"", p->u.u);
	} else if (p->flags & MEM_Real) {
		printf(" r:%g", p->u.r);
	} else if (p->flags & MEM_Bool) {
		printf(" bool:%s", SQL_TOKEN_BOOLEAN(p->u.b));
	} else {
		char zBuf[200];
		sqlVdbeMemPrettyPrint(p, zBuf);
		printf(" %s", zBuf);
	}
	if (p->flags & MEM_Subtype) printf(" subtype=0x%02x", p->subtype);
}

void
registerTrace(int iReg, Mem *p) {
	printf("REG[%d] = ", iReg);
	memTracePrint(p);
	printf("\n");
}
#endif

int
mem_apply_numeric_type(struct Mem *record)
{
	if ((record->flags & MEM_Str) == 0)
		return -1;
	int64_t integer_value;
	bool is_neg;
	if (sql_atoi64(record->z, &integer_value, &is_neg, record->n) == 0) {
		mem_set_int(record, integer_value, is_neg);
		return 0;
	}
	double float_value;
	if (sqlAtoF(record->z, &float_value, record->n) == 0)
		return -1;
	mem_set_double(record, float_value);
	return 0;
}

/*
 * Convert pMem so that it is of type MEM_Real.
 * Invalidate any prior representations.
 */
int
sqlVdbeMemRealify(Mem * pMem)
{
	assert(EIGHT_BYTE_ALIGNMENT(pMem));
	double v;
	if (sqlVdbeRealValue(pMem, &v))
		return -1;
	mem_set_double(pMem, v);
	return 0;
}

int
vdbe_mem_numerify(struct Mem *mem)
{
	if ((mem->flags & (MEM_Int | MEM_UInt | MEM_Real | MEM_Null)) != 0)
		return 0;
	if ((mem->flags & MEM_Bool) != 0) {
		mem->u.u = mem->u.b;
		MemSetTypeFlag(mem, MEM_UInt);
		return 0;
	}
	assert((mem->flags & (MEM_Blob | MEM_Str)) != 0);
	bool is_neg;
	int64_t i;
	if (sql_atoi64(mem->z, &i, &is_neg, mem->n) == 0) {
		mem_set_int(mem, i, is_neg);
	} else {
		double d;
		if (sqlAtoF(mem->z, &d, mem->n) == 0)
			return -1;
		mem_set_double(mem, d);
	}
	return 0;
}

/*
 * Cast the datatype of the value in pMem according to the type
 * @type.  Casting is different from applying type in that a cast
 * is forced.  In other words, the value is converted into the desired
 * type even if that results in loss of data.  This routine is
 * used (for example) to implement the SQL "cast()" operator.
 */
int
sqlVdbeMemCast(Mem * pMem, enum field_type type)
{
	assert(type < field_type_MAX);
	if (pMem->flags & MEM_Null)
		return 0;
	switch (type) {
	case FIELD_TYPE_SCALAR:
		return 0;
	case FIELD_TYPE_BOOLEAN:
		if ((pMem->flags & MEM_Int) != 0) {
			mem_set_bool(pMem, pMem->u.i);
			return 0;
		}
		if ((pMem->flags & MEM_UInt) != 0) {
			mem_set_bool(pMem, pMem->u.u);
			return 0;
		}
		if ((pMem->flags & MEM_Real) != 0) {
			mem_set_bool(pMem, pMem->u.r);
			return 0;
		}
		if ((pMem->flags & MEM_Str) != 0) {
			bool value;
			if (str_cast_to_boolean(pMem->z, &value) != 0)
				return -1;
			mem_set_bool(pMem, value);
			return 0;
		}
		if ((pMem->flags & MEM_Bool) != 0)
			return 0;
		return -1;
	case FIELD_TYPE_INTEGER:
	case FIELD_TYPE_UNSIGNED:
		if ((pMem->flags & (MEM_Blob | MEM_Str)) != 0) {
			bool is_neg;
			int64_t val;
			if (sql_atoi64(pMem->z, &val, &is_neg, pMem->n) != 0)
				return -1;
			if (type == FIELD_TYPE_UNSIGNED && is_neg)
				return -1;
			mem_set_int(pMem, val, is_neg);
			return 0;
		}
		if ((pMem->flags & MEM_Bool) != 0) {
			pMem->u.u = pMem->u.b;
			MemSetTypeFlag(pMem, MEM_UInt);
			return 0;
		}
		if ((pMem->flags & MEM_Real) != 0) {
			double d;
			if (sqlVdbeRealValue(pMem, &d) != 0)
				return -1;
			if (d < (double)INT64_MAX && d >= (double)INT64_MIN) {
				mem_set_int(pMem, d, d <= -1);
				return 0;
			}
			if (d >= (double)INT64_MAX && d < (double)UINT64_MAX) {
				mem_set_u64(pMem, d);
				return 0;
			}
			return -1;
		}
		if (type == FIELD_TYPE_UNSIGNED &&
		    (pMem->flags & MEM_UInt) == 0)
			return -1;
		return 0;
	case FIELD_TYPE_DOUBLE:
		return sqlVdbeMemRealify(pMem);
	case FIELD_TYPE_NUMBER:
		return vdbe_mem_numerify(pMem);
	case FIELD_TYPE_VARBINARY:
		if ((pMem->flags & MEM_Blob) != 0)
			return 0;
		if ((pMem->flags & MEM_Str) != 0) {
			MemSetTypeFlag(pMem, MEM_Str);
			return 0;
		}
		return -1;
	default:
		assert(type == FIELD_TYPE_STRING);
		assert(MEM_Str == (MEM_Blob >> 3));
		if ((pMem->flags & MEM_Bool) != 0) {
			const char *str_bool = SQL_TOKEN_BOOLEAN(pMem->u.b);
			sqlVdbeMemSetStr(pMem, str_bool, strlen(str_bool), 1,
					 SQL_TRANSIENT);
			return 0;
		}
		pMem->flags |= (pMem->flags & MEM_Blob) >> 3;
			sql_value_apply_type(pMem, FIELD_TYPE_STRING);
		assert(pMem->flags & MEM_Str || pMem->db->mallocFailed);
		pMem->flags &=
			~(MEM_Int | MEM_UInt | MEM_Real | MEM_Blob | MEM_Zero);
		return 0;
	}
}

/*
 * The MEM structure is already a MEM_Real.  Try to also make it a
 * MEM_Int if we can.
 */
int
mem_apply_integer_type(Mem *pMem)
{
	int rc;
	i64 ix;
	assert(pMem->flags & MEM_Real);
	assert(EIGHT_BYTE_ALIGNMENT(pMem));

	if ((rc = doubleToInt64(pMem->u.r, (int64_t *) &ix)) == 0)
		mem_set_int(pMem, ix, pMem->u.r <= -1);
	return rc;
}

/*
 * Add MEM_Str to the set of representations for the given Mem.  Numbers
 * are converted using sql_snprintf().  Converting a BLOB to a string
 * is a no-op.
 *
 * Existing representations MEM_Int and MEM_Real are invalidated if
 * bForce is true but are retained if bForce is false.
 *
 * A MEM_Null value will never be passed to this function. This function is
 * used for converting values to text for returning to the user (i.e. via
 * sql_value_text()), or for ensuring that values to be used as btree
 * keys are strings. In the former case a NULL pointer is returned the
 * user and the latter is an internal programming error.
 */
int
sqlVdbeMemStringify(Mem * pMem)
{
	int fg = pMem->flags;
	int nByte = 32;

	if ((fg & (MEM_Null | MEM_Str | MEM_Blob)) != 0 &&
	    !mem_has_msgpack_subtype(pMem))
		return 0;

	assert(!(fg & MEM_Zero));
	assert((fg & (MEM_Int | MEM_UInt | MEM_Real | MEM_Bool |
		      MEM_Blob)) != 0);
	assert(EIGHT_BYTE_ALIGNMENT(pMem));

	/*
	 * In case we have ARRAY/MAP we should save decoded value
	 * before clearing pMem->z.
	 */
	char *value = NULL;
	if (mem_has_msgpack_subtype(pMem)) {
		const char *value_str = mp_str(pMem->z);
		nByte = strlen(value_str) + 1;
		value = region_alloc(&fiber()->gc, nByte);
		memcpy(value, value_str, nByte);
	}

	if (sqlVdbeMemClearAndResize(pMem, nByte)) {
		return -1;
	}
	if (fg & MEM_Int) {
		sql_snprintf(nByte, pMem->z, "%lld", pMem->u.i);
		pMem->flags &= ~MEM_Int;
	} else if ((fg & MEM_UInt) != 0) {
		sql_snprintf(nByte, pMem->z, "%llu", pMem->u.u);
		pMem->flags &= ~MEM_UInt;
	} else if ((fg & MEM_Bool) != 0) {
		sql_snprintf(nByte, pMem->z, "%s",
			     SQL_TOKEN_BOOLEAN(pMem->u.b));
		pMem->flags &= ~MEM_Bool;
	} else if (mem_has_msgpack_subtype(pMem)) {
		sql_snprintf(nByte, pMem->z, "%s", value);
		pMem->flags &= ~MEM_Subtype;
		pMem->subtype = SQL_SUBTYPE_NO;
	} else {
		assert(fg & MEM_Real);
		sql_snprintf(nByte, pMem->z, "%!.15g", pMem->u.r);
		pMem->flags &= ~MEM_Real;
	}
	pMem->n = sqlStrlen30(pMem->z);
	pMem->flags |= MEM_Str | MEM_Term;
	return 0;
}

/*
 * Make sure the given Mem is \u0000 terminated.
 */
int
sqlVdbeMemNulTerminate(Mem * pMem)
{
	testcase((pMem->flags & (MEM_Term | MEM_Str)) == (MEM_Term | MEM_Str));
	testcase((pMem->flags & (MEM_Term | MEM_Str)) == 0);
	if ((pMem->flags & (MEM_Term | MEM_Str)) != MEM_Str) {
		return 0;	/* Nothing to do */
	} else {
		return vdbeMemAddTerminator(pMem);
	}
}

/*
 * If the given Mem* has a zero-filled tail, turn it into an ordinary
 * blob stored in dynamically allocated space.
 */
int
sqlVdbeMemExpandBlob(Mem * pMem)
{
	int nByte;
	assert(pMem->flags & MEM_Zero);
	assert(pMem->flags & MEM_Blob);

	/* Set nByte to the number of bytes required to store the expanded blob. */
	nByte = pMem->n + pMem->u.nZero;
	if (nByte <= 0) {
		nByte = 1;
	}
	if (sqlVdbeMemGrow(pMem, nByte, 1)) {
		return -1;
	}

	memset(&pMem->z[pMem->n], 0, pMem->u.nZero);
	pMem->n += pMem->u.nZero;
	pMem->flags &= ~(MEM_Zero | MEM_Term);
	return 0;
}

/*
 * Exported version of mem_apply_type(). This one works on sql_value*,
 * not the internal Mem* type.
 */
void
sql_value_apply_type(
	sql_value *pVal,
	enum field_type type)
{
	mem_apply_type((Mem *) pVal, type);
}

int
mem_apply_type(struct Mem *record, enum field_type type)
{
	if ((record->flags & MEM_Null) != 0)
		return 0;
	assert(type < field_type_MAX);
	switch (type) {
	case FIELD_TYPE_INTEGER:
	case FIELD_TYPE_UNSIGNED:
		if ((record->flags & (MEM_Bool | MEM_Blob)) != 0)
			return -1;
		if ((record->flags & MEM_UInt) == MEM_UInt)
			return 0;
		if ((record->flags & MEM_Real) == MEM_Real) {
			double d = record->u.r;
			if (d >= 0) {
				if (double_compare_uint64(d, UINT64_MAX,
							  1) > 0)
					return 0;
				if ((double)(uint64_t)d == d)
					mem_set_u64(record, (uint64_t)d);
			} else {
				if (double_compare_nint64(d, INT64_MIN, 1) < 0)
					return 0;
				if ((double)(int64_t)d == d)
					mem_set_int(record, (int64_t)d, true);
			}
			return 0;
		}
		if ((record->flags & MEM_Str) != 0) {
			bool is_neg;
			int64_t i;
			if (sql_atoi64(record->z, &i, &is_neg, record->n) != 0)
				return -1;
			mem_set_int(record, i, is_neg);
		}
		if ((record->flags & MEM_Int) == MEM_Int) {
			if (type == FIELD_TYPE_UNSIGNED)
				return -1;
			return 0;
		}
		return 0;
	case FIELD_TYPE_BOOLEAN:
		if ((record->flags & MEM_Bool) == MEM_Bool)
			return 0;
		return -1;
	case FIELD_TYPE_NUMBER:
		if ((record->flags & (MEM_Real | MEM_Int | MEM_UInt)) != 0)
			return 0;
		return sqlVdbeMemRealify(record);
	case FIELD_TYPE_DOUBLE:
		if ((record->flags & MEM_Real) != 0)
			return 0;
		return sqlVdbeMemRealify(record);
	case FIELD_TYPE_STRING:
		/*
		 * Only attempt the conversion to TEXT if there is
		 * an integer or real representation (BLOB and
		 * NULL do not get converted).
		 */
		if ((record->flags & MEM_Str) == 0 &&
		    (record->flags & (MEM_Real | MEM_Int | MEM_UInt)) != 0)
			sqlVdbeMemStringify(record);
		record->flags &= ~(MEM_Real | MEM_Int | MEM_UInt);
		return 0;
	case FIELD_TYPE_VARBINARY:
		if ((record->flags & MEM_Blob) == 0)
			return -1;
		return 0;
	case FIELD_TYPE_SCALAR:
		/* Can't cast MAP and ARRAY to scalar types. */
		if ((record->flags & MEM_Subtype) != 0 &&
		    record->subtype == SQL_SUBTYPE_MSGPACK) {
			assert(mp_typeof(*record->z) == MP_MAP ||
			       mp_typeof(*record->z) == MP_ARRAY);
			return -1;
		}
		return 0;
	case FIELD_TYPE_MAP:
		if ((record->flags & MEM_Subtype) != 0 &&
		    record->subtype == SQL_SUBTYPE_MSGPACK &&
		    mp_typeof(*record->z) == MP_MAP)
			return 0;
		return -1;
	case FIELD_TYPE_ARRAY:
		if ((record->flags & MEM_Subtype) != 0 &&
		    record->subtype == SQL_SUBTYPE_MSGPACK &&
		    mp_typeof(*record->z) == MP_ARRAY)
			return 0;
		return -1;
	case FIELD_TYPE_ANY:
		return 0;
	default:
		return -1;
	}
}

/**
 * Convert the numeric value contained in MEM to double.
 *
 * @param mem The MEM that contains the numeric value.
 * @retval 0 if the conversion was successful, -1 otherwise.
 */
static int
mem_convert_to_double(struct Mem *mem)
{
	if ((mem->flags & MEM_Real) != 0)
		return 0;
	if ((mem->flags & (MEM_Int | MEM_UInt)) == 0)
		return -1;
	double d;
	if ((mem->flags & MEM_Int) != 0)
		d = (double)mem->u.i;
	else
		d = (double)mem->u.u;
	mem_set_double(mem, d);
	return 0;
}

/**
 * Convert the numeric value contained in MEM to unsigned.
 *
 * @param mem The MEM that contains the numeric value.
 * @retval 0 if the conversion was successful, -1 otherwise.
 */
static int
mem_convert_to_unsigned(struct Mem *mem)
{
	if ((mem->flags & MEM_UInt) != 0)
		return 0;
	if ((mem->flags & MEM_Int) != 0)
		return -1;
	if ((mem->flags & MEM_Real) == 0)
		return -1;
	double d = mem->u.r;
	if (d < 0.0 || d >= (double)UINT64_MAX)
		return -1;
	mem_set_u64(mem, (uint64_t) d);
	return 0;
}

/**
 * Convert the numeric value contained in MEM to integer.
 *
 * @param mem The MEM that contains the numeric value.
 * @retval 0 if the conversion was successful, -1 otherwise.
 */
static int
mem_convert_to_integer(struct Mem *mem)
{
	if ((mem->flags & (MEM_UInt | MEM_Int)) != 0)
		return 0;
	if ((mem->flags & MEM_Real) == 0)
		return -1;
	double d = mem->u.r;
	if (d >= (double)UINT64_MAX || d < (double)INT64_MIN)
		return -1;
	if (d < (double)INT64_MAX)
		mem_set_int(mem, (int64_t) d, d < 0);
	else
		mem_set_int(mem, (uint64_t) d, false);
	return 0;
}

int
mem_convert_to_numeric(struct Mem *mem, enum field_type type)
{
	assert(mem_is_number(mem) && sql_type_is_numeric(type));
	assert(type != FIELD_TYPE_NUMBER);
	if (type == FIELD_TYPE_DOUBLE)
		return mem_convert_to_double(mem);
	if (type == FIELD_TYPE_UNSIGNED)
		return mem_convert_to_unsigned(mem);
	assert(type == FIELD_TYPE_INTEGER);
	return mem_convert_to_integer(mem);
}

/*
 * Make sure pMem->z points to a writable allocation of at least
 * min(n,32) bytes.
 *
 * If the bPreserve argument is true, then copy of the content of
 * pMem->z into the new allocation.  pMem must be either a string or
 * blob if bPreserve is true.  If bPreserve is false, any prior content
 * in pMem->z is discarded.
 */
SQL_NOINLINE int
sqlVdbeMemGrow(Mem * pMem, int n, int bPreserve)
{
	assert(sqlVdbeCheckMemInvariants(pMem));
	testcase(pMem->db == 0);

	/* If the bPreserve flag is set to true, then the memory cell must already
	 * contain a valid string or blob value.
	 */
	assert(bPreserve == 0 || pMem->flags & (MEM_Blob | MEM_Str));
	testcase(bPreserve && pMem->z == 0);

	assert(pMem->szMalloc == 0 ||
	       pMem->szMalloc == sqlDbMallocSize(pMem->db, pMem->zMalloc));
	if (pMem->szMalloc < n) {
		if (n < 32)
			n = 32;
		if (bPreserve && pMem->szMalloc > 0 && pMem->z == pMem->zMalloc) {
			pMem->z = pMem->zMalloc =
			    sqlDbReallocOrFree(pMem->db, pMem->z, n);
			bPreserve = 0;
		} else {
			if (pMem->szMalloc > 0)
				sqlDbFree(pMem->db, pMem->zMalloc);
			pMem->zMalloc = sqlDbMallocRaw(pMem->db, n);
		}
		if (pMem->zMalloc == 0) {
			sqlVdbeMemSetNull(pMem);
			pMem->z = 0;
			pMem->szMalloc = 0;
			return -1;
		} else {
			pMem->szMalloc = sqlDbMallocSize(pMem->db,
							 pMem->zMalloc);
		}
	}

	if (bPreserve && pMem->z && pMem->z != pMem->zMalloc) {
		memcpy(pMem->zMalloc, pMem->z, pMem->n);
	}
	if ((pMem->flags & MEM_Dyn) != 0) {
		assert(pMem->xDel != 0 && pMem->xDel != SQL_DYNAMIC);
		pMem->xDel((void *)(pMem->z));
	}

	pMem->z = pMem->zMalloc;
	pMem->flags &= ~(MEM_Dyn | MEM_Ephem | MEM_Static);
	return 0;
}

/*
 * Change the pMem->zMalloc allocation to be at least szNew bytes.
 * If pMem->zMalloc already meets or exceeds the requested size, this
 * routine is a no-op.
 *
 * Any prior string or blob content in the pMem object may be discarded.
 * The pMem->xDel destructor is called, if it exists.  Though MEM_Str
 * and MEM_Blob values may be discarded, MEM_Int, MEM_Real, and MEM_Null
 * values are preserved.
 *
 * Return 0 on success or -1 if unable to complete the resizing.
 */
int
sqlVdbeMemClearAndResize(Mem * pMem, int szNew)
{
	assert(szNew > 0);
	assert((pMem->flags & MEM_Dyn) == 0 || pMem->szMalloc == 0);
	if (pMem->szMalloc < szNew) {
		return sqlVdbeMemGrow(pMem, szNew, 0);
	}
	assert((pMem->flags & MEM_Dyn) == 0);
	pMem->z = pMem->zMalloc;
	pMem->flags &= (MEM_Null | MEM_Int | MEM_Real);
	return 0;
}

void
mem_set_bool(struct Mem *mem, bool value)
{
	sqlVdbeMemSetNull(mem);
	mem->u.b = value;
	mem->flags = MEM_Bool;
	mem->field_type = FIELD_TYPE_BOOLEAN;
}

void
mem_set_ptr(struct Mem *mem, void *ptr)
{
	mem_destroy(mem);
	mem->flags = MEM_Ptr;
	mem->u.p = ptr;
}

void
mem_set_i64(struct Mem *mem, int64_t value)
{
	if (VdbeMemDynamic(mem))
		sqlVdbeMemSetNull(mem);
	mem->u.i = value;
	int flag = value < 0 ? MEM_Int : MEM_UInt;
	MemSetTypeFlag(mem, flag);
	mem->field_type = FIELD_TYPE_INTEGER;
}

void
mem_set_u64(struct Mem *mem, uint64_t value)
{
	if (VdbeMemDynamic(mem))
		sqlVdbeMemSetNull(mem);
	mem->u.u = value;
	MemSetTypeFlag(mem, MEM_UInt);
	mem->field_type = FIELD_TYPE_UNSIGNED;
}

void
mem_set_int(struct Mem *mem, int64_t value, bool is_neg)
{
	if (VdbeMemDynamic(mem))
		sqlVdbeMemSetNull(mem);
	if (is_neg) {
		assert(value < 0);
		mem->u.i = value;
		MemSetTypeFlag(mem, MEM_Int);
	} else {
		mem->u.u = value;
		MemSetTypeFlag(mem, MEM_UInt);
	}
	mem->field_type = FIELD_TYPE_INTEGER;
}

void
mem_set_double(struct Mem *mem, double value)
{
	sqlVdbeMemSetNull(mem);
	if (sqlIsNaN(value))
		return;
	mem->u.r = value;
	MemSetTypeFlag(mem, MEM_Real);
	mem->field_type = FIELD_TYPE_DOUBLE;
}

/*
 * Change the value of a Mem to be a string or a BLOB.
 *
 * The memory management strategy depends on the value of the xDel
 * parameter. If the value passed is SQL_TRANSIENT, then the
 * string is copied into a (possibly existing) buffer managed by the
 * Mem structure. Otherwise, any existing buffer is freed and the
 * pointer copied.
 *
 * If the string is too large (if it exceeds the SQL_LIMIT_LENGTH
 * size limit) then no memory allocation occurs.  If the string can be
 * stored without allocating memory, then it is.  If a memory allocation
 * is required to store the string, then value of pMem is unchanged.  In
 * either case, error is returned.
 */
int
sqlVdbeMemSetStr(Mem * pMem,	/* Memory cell to set to string value */
		     const char *z,	/* String pointer */
		     int n,	/* Bytes in string, or negative */
		     u8 not_blob,	/* Encoding of z.  0 for BLOBs */
		     void (*xDel) (void *)	/* Destructor function */
    )
{
	int nByte = n;		/* New value for pMem->n */
	int iLimit;		/* Maximum allowed string or blob size */
	u16 flags = 0;		/* New value for pMem->flags */

	/* If z is a NULL pointer, set pMem to contain an SQL NULL. */
	if (!z) {
		sqlVdbeMemSetNull(pMem);
		return 0;
	}

	if (pMem->db) {
		iLimit = pMem->db->aLimit[SQL_LIMIT_LENGTH];
	} else {
		iLimit = SQL_MAX_LENGTH;
	}
	flags = (not_blob == 0 ? MEM_Blob : MEM_Str);
	if (nByte < 0) {
		assert(not_blob != 0);
		nByte = sqlStrlen30(z);
		if (nByte > iLimit)
			nByte = iLimit + 1;
		flags |= MEM_Term;
	}

	/* The following block sets the new values of Mem.z and Mem.xDel. It
	 * also sets a flag in local variable "flags" to indicate the memory
	 * management (one of MEM_Dyn or MEM_Static).
	 */
	if (xDel == SQL_TRANSIENT) {
		int nAlloc = nByte;
		if (flags & MEM_Term) {
			nAlloc += 1; //SQL_UTF8
		}
		if (nByte > iLimit) {
			diag_set(ClientError, ER_SQL_EXECUTE, "string or binary"\
				 "string is too big");
			return -1;
		}
		testcase(nAlloc == 0);
		testcase(nAlloc == 31);
		testcase(nAlloc == 32);
		if (sqlVdbeMemClearAndResize(pMem, MAX(nAlloc, 32))) {
			return -1;
		}
		memcpy(pMem->z, z, nAlloc);
	} else if (xDel == SQL_DYNAMIC) {
		mem_destroy(pMem);
		pMem->zMalloc = pMem->z = (char *)z;
		pMem->szMalloc = sqlDbMallocSize(pMem->db, pMem->zMalloc);
	} else {
		mem_destroy(pMem);
		pMem->z = (char *)z;
		pMem->xDel = xDel;
		flags |= ((xDel == SQL_STATIC) ? MEM_Static : MEM_Dyn);
	}

	pMem->n = nByte;
	pMem->flags = flags;
	assert((pMem->flags & (MEM_Str | MEM_Blob)) != 0);
	if ((pMem->flags & MEM_Str) != 0)
		pMem->field_type = FIELD_TYPE_STRING;
	else
		pMem->field_type = FIELD_TYPE_VARBINARY;

	if (nByte > iLimit) {
		diag_set(ClientError, ER_SQL_EXECUTE, "string or binary string"\
			 "is too big");
		return -1;
	}

	return 0;
}

/*
 * Delete any previous value and set the value stored in *pMem to NULL.
 *
 * This routine calls the Mem.xDel destructor to dispose of values that
 * require the destructor.  But it preserves the Mem.zMalloc memory allocation.
 * To free all resources, use mem_destroy(), which both calls this
 * routine to invoke the destructor and deallocates Mem.zMalloc.
 *
 * Use this routine to reset the Mem prior to insert a new value.
 *
 * Use mem_destroy() to complete erase the Mem prior to abandoning it.
 */
void
sqlVdbeMemSetNull(Mem * pMem)
{
	if (VdbeMemDynamic(pMem)) {
		mem_clear(pMem);
	} else {
		pMem->flags = MEM_Null;
	}
}

/*
 * Delete any previous value and set the value to be a BLOB of length
 * n containing all zeros.
 */
void
sqlVdbeMemSetZeroBlob(Mem * pMem, int n)
{
	mem_destroy(pMem);
	pMem->flags = MEM_Blob | MEM_Zero;
	pMem->n = 0;
	if (n < 0)
		n = 0;
	pMem->u.nZero = n;
	pMem->z = 0;
}

/*
 * Change the string value of an sql_value object
 */
void
sqlValueSetStr(sql_value * v,	/* Value to be set */
		   int n,	/* Length of string z */
		   const void *z,	/* Text of the new string */
		   void (*xDel) (void *)	/* Destructor for the string */
    )
{
	if (v)
		sqlVdbeMemSetStr((Mem *) v, z, n, 1, xDel);
}

void
sqlValueSetNull(sql_value * p)
{
	sqlVdbeMemSetNull((Mem *) p);
}

/*
 * Free an sql_value object
 */
void
sqlValueFree(sql_value * v)
{
	if (!v)
		return;
	mem_destroy((Mem *) v);
	sqlDbFree(((Mem *) v)->db, v);
}

/*
 * Create a new sql_value object.
 */
sql_value *
sqlValueNew(sql * db)
{
	Mem *p = sqlDbMallocZero(db, sizeof(*p));
	if (p) {
		p->flags = MEM_Null;
		p->db = db;
	}
	return p;
}

void
initMemArray(Mem * p, int N, sql * db, u32 flags)
{
	while ((N--) > 0) {
		p->db = db;
		p->flags = flags;
		p->szMalloc = 0;
		p->field_type = field_type_MAX;
#ifdef SQL_DEBUG
		p->pScopyFrom = 0;
#endif
		p++;
	}
}

void
releaseMemArray(Mem * p, int N)
{
	if (p && N) {
		Mem *pEnd = &p[N];
		do {
			assert((&p[1]) == pEnd || p[0].db == p[1].db);
			assert(sqlVdbeCheckMemInvariants(p));
			mem_destroy(p);
			p->flags = MEM_Undefined;
		} while ((++p) < pEnd);
	}
}

int
mem_value_bool(const struct Mem *mem, bool *b)
{
	if ((mem->flags  & MEM_Bool) != 0) {
		*b = mem->u.b;
		return 0;
	}
	return -1;
}

/*
 * Return some kind of integer value which is the best we can do
 * at representing the value that *pMem describes as an integer.
 * If pMem is an integer, then the value is exact.  If pMem is
 * a floating-point then the value returned is the integer part.
 * If pMem is a string or blob, then we make an attempt to convert
 * it into an integer and return that.  If pMem represents an
 * an SQL-NULL value, return 0.
 *
 * If pMem represents a string value, its encoding might be changed.
 */
int
sqlVdbeIntValue(Mem * pMem, int64_t *i, bool *is_neg)
{
	int flags;
	assert(EIGHT_BYTE_ALIGNMENT(pMem));
	flags = pMem->flags;
	if (flags & MEM_Int) {
		*i = pMem->u.i;
		*is_neg = true;
		return 0;
	} else if (flags & MEM_UInt) {
		*i = pMem->u.u;
		*is_neg = false;
		return 0;
	} else if (flags & MEM_Real) {
		*is_neg = pMem->u.r < 0;
		return doubleToInt64(pMem->u.r, i);
	} else if (flags & (MEM_Str)) {
		assert(pMem->z || pMem->n == 0);
		if (sql_atoi64(pMem->z, i, is_neg, pMem->n) == 0)
			return 0;
	}
	return -1;
}

/*
 * Return the best representation of pMem that we can get into a
 * double.  If pMem is already a double or an integer, return its
 * value.  If it is a string or blob, try to convert it to a double.
 * If it is a NULL, return 0.0.
 */
int
sqlVdbeRealValue(Mem * pMem, double *v)
{
	assert(EIGHT_BYTE_ALIGNMENT(pMem));
	if (pMem->flags & MEM_Real) {
		*v = pMem->u.r;
		return 0;
	} else if (pMem->flags & MEM_Int) {
		*v = (double)pMem->u.i;
		return 0;
	} else if ((pMem->flags & MEM_UInt) != 0) {
		*v = (double)pMem->u.u;
		return 0;
	} else if (pMem->flags & MEM_Str) {
		if (sqlAtoF(pMem->z, v, pMem->n))
			return 0;
	}
	return -1;
}

/**************************** sql_value_  ******************************
 * The following routines extract information from a Mem or sql_value
 * structure.
 */
const void *
sql_value_blob(sql_value * pVal)
{
	Mem *p = (Mem *) pVal;
	if (p->flags & (MEM_Blob | MEM_Str)) {
		if (ExpandBlob(p) != 0) {
			assert(p->flags == MEM_Null && p->z == 0);
			return 0;
		}
		p->flags |= MEM_Blob;
		return p->n ? p->z : 0;
	} else {
		return sql_value_text(pVal);
	}
}

int
sql_value_bytes(sql_value * pVal)
{
	return sqlValueBytes(pVal);
}

double
sql_value_double(sql_value * pVal)
{
	double v = 0.0;
	sqlVdbeRealValue((Mem *) pVal, &v);
	return v;
}

bool
sql_value_boolean(sql_value *val)
{
	bool b = false;
	int rc = mem_value_bool((struct Mem *) val, &b);
	assert(rc == 0);
	(void) rc;
	return b;
}

int
sql_value_int(sql_value * pVal)
{
	int64_t i = 0;
	bool is_neg;
	sqlVdbeIntValue((Mem *) pVal, &i, &is_neg);
	return (int)i;
}

sql_int64
sql_value_int64(sql_value * pVal)
{
	int64_t i = 0;
	bool unused;
	sqlVdbeIntValue((Mem *) pVal, &i, &unused);
	return i;
}

uint64_t
sql_value_uint64(sql_value *val)
{
	int64_t i = 0;
	bool is_neg;
	sqlVdbeIntValue((struct Mem *) val, &i, &is_neg);
	assert(!is_neg);
	return i;
}

const unsigned char *
sql_value_text(sql_value * pVal)
{
	return (const unsigned char *)sqlValueText(pVal);
}

/* This function is only available internally, it is not part of the
 * external API. It works in a similar way to sql_value_text(),
 * except the data returned is in the encoding specified by the second
 * parameter, which must be one of SQL_UTF16BE, SQL_UTF16LE or
 * SQL_UTF8.
 *
 * (2006-02-16:)  The enc value can be or-ed with SQL_UTF16_ALIGNED.
 * If that is the case, then the result must be aligned on an even byte
 * boundary.
 */
const void *
sqlValueText(sql_value * pVal)
{
	if (!pVal)
		return 0;
	if ((pVal->flags & (MEM_Str | MEM_Term)) == (MEM_Str | MEM_Term)) {
		return pVal->z;
	}
	if (pVal->flags & MEM_Null) {
		return 0;
	}
	return valueToText(pVal);
}

const char *
sql_value_to_diag_str(sql_value *value)
{
	if (mem_is_binary(value)) {
		if (mem_has_msgpack_subtype(value))
			return sqlValueText(value);
		return "varbinary";
	}
	return sqlValueText(value);
}

enum sql_subtype
sql_value_subtype(sql_value * pVal)
{
	return (pVal->flags & MEM_Subtype) != 0 ? pVal->subtype : SQL_SUBTYPE_NO;
}

/*
 * Return a pointer to static memory containing an SQL NULL value.
 */
const Mem *
columnNullValue(void)
{
	/* Even though the Mem structure contains an element
	 * of type i64, on certain architectures (x86) with certain compiler
	 * switches (-Os), gcc may align this Mem object on a 4-byte boundary
	 * instead of an 8-byte one. This all works fine, except that when
	 * running with SQL_DEBUG defined the sql code sometimes assert()s
	 * that a Mem structure is located on an 8-byte boundary. To prevent
	 * these assert()s from failing, when building with SQL_DEBUG defined
	 * using gcc, we force nullMem to be 8-byte aligned using the magical
	 * __attribute__((aligned(8))) macro.
	 */
	static const Mem nullMem
#if defined(SQL_DEBUG) && defined(__GNUC__)
	    __attribute__ ((aligned(8)))
#endif
	    = {
		/* .u          = */  {
		0},
		    /* .flags      = */ (u16) MEM_Null,
		    /* .eSubtype   = */ (u8) 0,
		    /* .field_type = */ field_type_MAX,
		    /* .n          = */ (int)0,
		    /* .z          = */ (char *)0,
		    /* .zMalloc    = */ (char *)0,
		    /* .szMalloc   = */ (int)0,
		    /* .uTemp      = */ (u32) 0,
		    /* .db         = */ (sql *) 0,
		    /* .xDel       = */ (void (*)(void *))0,
#ifdef SQL_DEBUG
		    /* .pScopyFrom = */ (Mem *) 0,
		    /* .pFiller    = */ (void *)0,
#endif
	};
	return &nullMem;
}

/*
 * Return true if the Mem object contains a TEXT or BLOB that is
 * too large - whose size exceeds SQL_MAX_LENGTH.
 */
int
sqlVdbeMemTooBig(Mem * p)
{
	assert(p->db != 0);
	if (p->flags & (MEM_Str | MEM_Blob)) {
		int n = p->n;
		if (p->flags & MEM_Zero) {
			n += p->u.nZero;
		}
		return n > p->db->aLimit[SQL_LIMIT_LENGTH];
	}
	return 0;
}

/*
 * Compare two blobs.  Return negative, zero, or positive if the first
 * is less than, equal to, or greater than the second, respectively.
 * If one blob is a prefix of the other, then the shorter is the lessor.
 */
static SQL_NOINLINE int
sqlBlobCompare(const Mem * pB1, const Mem * pB2)
{
	int c;
	int n1 = pB1->n;
	int n2 = pB2->n;

	/* It is possible to have a Blob value that has some non-zero content
	 * followed by zero content.  But that only comes up for Blobs formed
	 * by the OP_MakeRecord opcode, and such Blobs never get passed into
	 * sqlMemCompare().
	 */
	assert((pB1->flags & MEM_Zero) == 0 || n1 == 0);
	assert((pB2->flags & MEM_Zero) == 0 || n2 == 0);

	if ((pB1->flags | pB2->flags) & MEM_Zero) {
		if (pB1->flags & pB2->flags & MEM_Zero) {
			return pB1->u.nZero - pB2->u.nZero;
		} else if (pB1->flags & MEM_Zero) {
			if (!isAllZero(pB2->z, pB2->n))
				return -1;
			return pB1->u.nZero - n2;
		} else {
			if (!isAllZero(pB1->z, pB1->n))
				return +1;
			return n1 - pB2->u.nZero;
		}
	}
	c = memcmp(pB1->z, pB2->z, n1 > n2 ? n2 : n1);
	if (c)
		return c;
	return n1 - n2;
}

/*
 * Compare the values contained by the two memory cells, returning
 * negative, zero or positive if pMem1 is less than, equal to, or greater
 * than pMem2. Sorting order is NULL's first, followed by numbers (integers
 * and reals) sorted numerically, followed by text ordered by the collating
 * sequence pColl and finally blob's ordered by memcmp().
 *
 * Two NULL values are considered equal by this function.
 */
int
sqlMemCompare(const Mem * pMem1, const Mem * pMem2, const struct coll * pColl)
{
	int f1, f2;
	int combined_flags;

	f1 = pMem1->flags;
	f2 = pMem2->flags;
	combined_flags = f1 | f2;

	/* If one value is NULL, it is less than the other. If both values
	 * are NULL, return 0.
	 */
	if (combined_flags & MEM_Null) {
		return (f2 & MEM_Null) - (f1 & MEM_Null);
	}

	if ((combined_flags & MEM_Bool) != 0) {
		if ((f1 & f2 & MEM_Bool) != 0) {
			if (pMem1->u.b == pMem2->u.b)
				return 0;
			if (pMem1->u.b)
				return 1;
			return -1;
		}
		if ((f2 & MEM_Bool) != 0)
			return +1;
		return -1;
	}

	/* At least one of the two values is a number
	 */
	if ((combined_flags & (MEM_Int | MEM_UInt | MEM_Real)) != 0) {
		if ((f1 & f2 & MEM_Int) != 0) {
			if (pMem1->u.i < pMem2->u.i)
				return -1;
			if (pMem1->u.i > pMem2->u.i)
				return +1;
			return 0;
		}
		if ((f1 & f2 & MEM_UInt) != 0) {
			if (pMem1->u.u < pMem2->u.u)
				return -1;
			if (pMem1->u.u > pMem2->u.u)
				return +1;
			return 0;
		}
		if ((f1 & f2 & MEM_Real) != 0) {
			if (pMem1->u.r < pMem2->u.r)
				return -1;
			if (pMem1->u.r > pMem2->u.r)
				return +1;
			return 0;
		}
		if ((f1 & MEM_Int) != 0) {
			if ((f2 & MEM_Real) != 0) {
				return double_compare_nint64(pMem2->u.r,
							     pMem1->u.i, -1);
			} else {
				return -1;
			}
		}
		if ((f1 & MEM_UInt) != 0) {
			if ((f2 & MEM_Real) != 0) {
				return double_compare_uint64(pMem2->u.r,
							     pMem1->u.u, -1);
			} else if ((f2 & MEM_Int) != 0) {
				return +1;
			} else {
				return -1;
			}
		}
		if ((f1 & MEM_Real) != 0) {
			if ((f2 & MEM_Int) != 0) {
				return double_compare_nint64(pMem1->u.r,
							     pMem2->u.i, 1);
			} else if ((f2 & MEM_UInt) != 0) {
				return double_compare_uint64(pMem1->u.r,
							     pMem2->u.u, 1);
			} else {
				return -1;
			}
		}
		return +1;
	}

	/* If one value is a string and the other is a blob, the string is less.
	 * If both are strings, compare using the collating functions.
	 */
	if (combined_flags & MEM_Str) {
		if ((f1 & MEM_Str) == 0) {
			return 1;
		}
		if ((f2 & MEM_Str) == 0) {
			return -1;
		}
		/* The collation sequence must be defined at this point, even if
		 * the user deletes the collation sequence after the vdbe program is
		 * compiled (this was not always the case).
		 */
		if (pColl) {
			return vdbeCompareMemString(pMem1, pMem2, pColl);
		} else {
			size_t n = pMem1->n < pMem2->n ? pMem1->n : pMem2->n;
			int res;
			res = memcmp(pMem1->z, pMem2->z, n);
			if (res == 0)
				res = (int)pMem1->n - (int)pMem2->n;
			return res;
		}
		/* If a NULL pointer was passed as the collate function, fall through
		 * to the blob case and use memcmp().
		 */
	}

	/* Both values must be blobs.  Compare using memcmp().  */
	return sqlBlobCompare(pMem1, pMem2);
}

bool
mem_is_type_compatible(struct Mem *mem, enum field_type type)
{
	enum mp_type mp_type = mem_mp_type(mem);
	assert(mp_type < MP_EXT);
	return field_mp_plain_type_is_compatible(type, mp_type, true);
}

/* Allocate memory for internal VDBE structure on region. */
int
vdbe_mem_alloc_blob_region(struct Mem *vdbe_mem, uint32_t size)
{
	vdbe_mem->n = size;
	vdbe_mem->z = region_alloc(&fiber()->gc, size);
	if (vdbe_mem->z == NULL)
		return -1;
	vdbe_mem->flags = MEM_Ephem | MEM_Blob;
	assert(sqlVdbeCheckMemInvariants(vdbe_mem));
	return 0;
}

int
sql_vdbemem_finalize(struct Mem *mem, struct func *func)
{
	assert(func != NULL);
	assert(func->def->language == FUNC_LANGUAGE_SQL_BUILTIN);
	assert(func->def->aggregate == FUNC_AGGREGATE_GROUP);
	assert((mem->flags & MEM_Null) != 0 || func == mem->u.func);
	sql_context ctx;
	memset(&ctx, 0, sizeof(ctx));
	Mem t;
	memset(&t, 0, sizeof(t));
	t.flags = MEM_Null;
	t.db = mem->db;
	t.field_type = field_type_MAX;
	ctx.pOut = &t;
	ctx.pMem = mem;
	ctx.func = func;
	((struct func_sql_builtin *)func)->finalize(&ctx);
	assert((mem->flags & MEM_Dyn) == 0);
	if (mem->szMalloc > 0)
		sqlDbFree(mem->db, mem->zMalloc);
	memcpy(mem, &t, sizeof(t));
	return ctx.is_aborted ? -1 : 0;
}

int
sqlVdbeCompareMsgpack(const char **key1,
			  struct UnpackedRecord *unpacked, int key2_idx)
{
	const char *aKey1 = *key1;
	Mem *pKey2 = unpacked->aMem + key2_idx;
	Mem mem1;
	int rc = 0;
	switch (mp_typeof(*aKey1)) {
	default:{
			/* FIXME */
			rc = -1;
			break;
		}
	case MP_NIL:{
			rc = -((pKey2->flags & MEM_Null) == 0);
			mp_decode_nil(&aKey1);
			break;
		}
	case MP_BOOL:{
			mem1.u.b = mp_decode_bool(&aKey1);
			if ((pKey2->flags & MEM_Bool) != 0) {
				if (mem1.u.b != pKey2->u.b)
					rc = mem1.u.b ? 1 : -1;
			} else {
				rc = (pKey2->flags & MEM_Null) != 0 ? 1 : -1;
			}
			break;
		}
	case MP_UINT:{
			mem1.u.u = mp_decode_uint(&aKey1);
			if ((pKey2->flags & MEM_Int) != 0) {
				rc = +1;
			} else if ((pKey2->flags & MEM_UInt) != 0) {
				if (mem1.u.u < pKey2->u.u)
					rc = -1;
				else if (mem1.u.u > pKey2->u.u)
					rc = +1;
			} else if ((pKey2->flags & MEM_Real) != 0) {
				rc = double_compare_uint64(pKey2->u.r,
							   mem1.u.u, -1);
			} else if ((pKey2->flags & MEM_Null) != 0) {
				rc = 1;
			} else if ((pKey2->flags & MEM_Bool) != 0) {
				rc = 1;
			} else {
				rc = -1;
			}
			break;
		}
	case MP_INT:{
			mem1.u.i = mp_decode_int(&aKey1);
			if ((pKey2->flags & MEM_UInt) != 0) {
				rc = -1;
			} else if ((pKey2->flags & MEM_Int) != 0) {
				if (mem1.u.i < pKey2->u.i) {
					rc = -1;
				} else if (mem1.u.i > pKey2->u.i) {
					rc = +1;
				}
			} else if (pKey2->flags & MEM_Real) {
				rc = double_compare_nint64(pKey2->u.r, mem1.u.i,
							   -1);
			} else if ((pKey2->flags & MEM_Null) != 0) {
				rc = 1;
			} else if ((pKey2->flags & MEM_Bool) != 0) {
				rc = 1;
			} else {
				rc = -1;
			}
			break;
		}
	case MP_FLOAT:{
			mem1.u.r = mp_decode_float(&aKey1);
			goto do_float;
		}
	case MP_DOUBLE:{
			mem1.u.r = mp_decode_double(&aKey1);
 do_float:
			if ((pKey2->flags & MEM_Int) != 0) {
				rc = double_compare_nint64(mem1.u.r, pKey2->u.i,
							   1);
			} else if (pKey2->flags & MEM_UInt) {
				rc = double_compare_uint64(mem1.u.r,
							   pKey2->u.u, 1);
			} else if (pKey2->flags & MEM_Real) {
				if (mem1.u.r < pKey2->u.r) {
					rc = -1;
				} else if (mem1.u.r > pKey2->u.r) {
					rc = +1;
				}
			} else if ((pKey2->flags & MEM_Null) != 0) {
				rc = 1;
			} else if ((pKey2->flags & MEM_Bool) != 0) {
				rc = 1;
			} else {
				rc = -1;
			}
			break;
		}
	case MP_STR:{
			if (pKey2->flags & MEM_Str) {
				struct key_def *key_def = unpacked->key_def;
				mem1.n = mp_decode_strl(&aKey1);
				mem1.z = (char *)aKey1;
				aKey1 += mem1.n;
				struct coll *coll =
					key_def->parts[key2_idx].coll;
				if (coll != NULL) {
					mem1.flags = MEM_Str;
					rc = vdbeCompareMemString(&mem1, pKey2,
								  coll);
				} else {
					goto do_bin_cmp;
				}
			} else {
				rc = (pKey2->flags & MEM_Blob) ? -1 : +1;
			}
			break;
		}
	case MP_BIN:{
			mem1.n = mp_decode_binl(&aKey1);
			mem1.z = (char *)aKey1;
			aKey1 += mem1.n;
 do_blob:
			if (pKey2->flags & MEM_Blob) {
				if (pKey2->flags & MEM_Zero) {
					if (!isAllZero
					    ((const char *)mem1.z, mem1.n)) {
						rc = 1;
					} else {
						rc = mem1.n - pKey2->u.nZero;
					}
				} else {
					int nCmp;
 do_bin_cmp:
					nCmp = MIN(mem1.n, pKey2->n);
					rc = memcmp(mem1.z, pKey2->z, nCmp);
					if (rc == 0)
						rc = mem1.n - pKey2->n;
				}
			} else {
				rc = 1;
			}
			break;
		}
	case MP_ARRAY:
	case MP_MAP:
	case MP_EXT:{
			mem1.z = (char *)aKey1;
			mp_next(&aKey1);
			mem1.n = aKey1 - (char *)mem1.z;
			goto do_blob;
		}
	}
	*key1 = aKey1;
	return rc;
}

int
sqlVdbeRecordCompareMsgpack(const void *key1,
				struct UnpackedRecord *key2)
{
	int rc = 0;
	u32 i, n = mp_decode_array((const char**)&key1);

	n = MIN(n, key2->nField);

	for (i = 0; i != n; i++) {
		rc = sqlVdbeCompareMsgpack((const char**)&key1, key2, i);
		if (rc != 0) {
			if (key2->key_def->parts[i].sort_order !=
			    SORT_ORDER_ASC) {
				rc = -rc;
			}
			return rc;
		}
	}

	key2->eqSeen = 1;
	return key2->default_rc;
}

int
vdbe_decode_msgpack_into_ephemeral_mem(const char *buf, struct Mem *mem,
				       uint32_t *len)
{
	const char *start_buf = buf;
	switch (mp_typeof(*buf)) {
	case MP_ARRAY: {
		mem->z = (char *)buf;
		mp_next(&buf);
		mem->n = buf - mem->z;
		mem->flags = MEM_Blob | MEM_Ephem | MEM_Subtype;
		mem->subtype = SQL_SUBTYPE_MSGPACK;
		mem->field_type = FIELD_TYPE_ARRAY;
		break;
	}
	case MP_MAP: {
		mem->z = (char *)buf;
		mp_next(&buf);
		mem->n = buf - mem->z;
		mem->flags = MEM_Blob | MEM_Ephem | MEM_Subtype;
		mem->subtype = SQL_SUBTYPE_MSGPACK;
		mem->field_type = FIELD_TYPE_MAP;
		break;
	}
	case MP_EXT: {
		mem->z = (char *)buf;
		mp_next(&buf);
		mem->n = buf - mem->z;
		mem->flags = MEM_Blob | MEM_Ephem;
		mem->field_type = FIELD_TYPE_VARBINARY;
		break;
	}
	case MP_NIL: {
		mp_decode_nil(&buf);
		mem->flags = MEM_Null;
		mem->field_type = field_type_MAX;
		break;
	}
	case MP_BOOL: {
		mem->u.b = mp_decode_bool(&buf);
		mem->flags = MEM_Bool;
		mem->field_type = FIELD_TYPE_BOOLEAN;
		break;
	}
	case MP_UINT: {
		uint64_t v = mp_decode_uint(&buf);
		mem->u.u = v;
		mem->flags = MEM_UInt;
		mem->field_type = FIELD_TYPE_INTEGER;
		break;
	}
	case MP_INT: {
		mem->u.i = mp_decode_int(&buf);
		mem->flags = MEM_Int;
		mem->field_type = FIELD_TYPE_INTEGER;
		break;
	}
	case MP_STR: {
		/* XXX u32->int */
		mem->n = (int) mp_decode_strl(&buf);
		mem->flags = MEM_Str | MEM_Ephem;
		mem->field_type = FIELD_TYPE_STRING;
install_blob:
		mem->z = (char *)buf;
		buf += mem->n;
		break;
	}
	case MP_BIN: {
		/* XXX u32->int */
		mem->n = (int) mp_decode_binl(&buf);
		mem->flags = MEM_Blob | MEM_Ephem;
		mem->field_type = FIELD_TYPE_VARBINARY;
		goto install_blob;
	}
	case MP_FLOAT: {
		mem->u.r = mp_decode_float(&buf);
		if (sqlIsNaN(mem->u.r)) {
			mem->flags = MEM_Null;
			mem->field_type = field_type_MAX;
		} else {
			mem->flags = MEM_Real;
			mem->field_type = FIELD_TYPE_DOUBLE;
		}
		break;
	}
	case MP_DOUBLE: {
		mem->u.r = mp_decode_double(&buf);
		if (sqlIsNaN(mem->u.r)) {
			mem->flags = MEM_Null;
			mem->field_type = field_type_MAX;
		} else {
			mem->flags = MEM_Real;
			mem->field_type = FIELD_TYPE_DOUBLE;
		}
		break;
	}
	default:
		unreachable();
	}
	*len = (uint32_t)(buf - start_buf);
	return 0;
}

int
vdbe_decode_msgpack_into_mem(const char *buf, struct Mem *mem, uint32_t *len)
{
	if (vdbe_decode_msgpack_into_ephemeral_mem(buf, mem, len) != 0)
		return -1;
	if ((mem->flags & (MEM_Str | MEM_Blob)) != 0) {
		assert((mem->flags & MEM_Ephem) != 0);
		if (sqlVdbeMemGrow(mem, mem->n, 1) != 0)
			return -1;
	}
	return 0;
}

void
mpstream_encode_vdbe_mem(struct mpstream *stream, struct Mem *var)
{
	assert(memIsValid(var));
	int64_t i;
	if (var->flags & MEM_Null) {
		mpstream_encode_nil(stream);
	} else if (var->flags & MEM_Real) {
		mpstream_encode_double(stream, var->u.r);
	} else if (var->flags & MEM_Int) {
		i = var->u.i;
		mpstream_encode_int(stream, i);
	} else if (var->flags & MEM_UInt) {
		i = var->u.u;
		mpstream_encode_uint(stream, i);
	} else if (var->flags & MEM_Str) {
		mpstream_encode_strn(stream, var->z, var->n);
	} else if (var->flags & MEM_Bool) {
		mpstream_encode_bool(stream, var->u.b);
	} else {
		/*
		 * Emit BIN header iff the BLOB doesn't store
		 * MsgPack content.
		 */
		if (!mem_has_msgpack_subtype(var)) {
			uint32_t binl = var->n +
					((var->flags & MEM_Zero) ?
					var->u.nZero : 0);
			mpstream_encode_binl(stream, binl);
		}
		mpstream_memcpy(stream, var->z, var->n);
		if (var->flags & MEM_Zero)
			mpstream_memset(stream, 0, var->u.nZero);
	}
}

char *
sql_vdbe_mem_encode_tuple(struct Mem *fields, uint32_t field_count,
			  uint32_t *tuple_size, struct region *region)
{
	size_t used = region_used(region);
	bool is_error = false;
	struct mpstream stream;
	mpstream_init(&stream, region, region_reserve_cb, region_alloc_cb,
		      set_encode_error, &is_error);
	mpstream_encode_array(&stream, field_count);
	for (struct Mem *field = fields; field < fields + field_count; field++)
		mpstream_encode_vdbe_mem(&stream, field);
	mpstream_flush(&stream);
	if (is_error) {
		diag_set(OutOfMemory, stream.pos - stream.buf,
			 "mpstream_flush", "stream");
		return NULL;
	}
	*tuple_size = region_used(region) - used;
	char *tuple = region_join(region, *tuple_size);
	if (tuple == NULL) {
		diag_set(OutOfMemory, *tuple_size, "region_join", "tuple");
		return NULL;
	}
	mp_tuple_assert(tuple, tuple + *tuple_size);
	return tuple;
}
