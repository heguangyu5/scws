/*
  +----------------------------------------------------------------------+
  | PHP Version 4                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2006 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author: hightman (hightman[AT]twomice.net) QQ = 16139558             |
  +----------------------------------------------------------------------+
*/

/* $Id$ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "php_scws.h"
#include <scws.h>

/// hightman.090716: for PHP5.3+
#if (PHP_MAJOR_VERSION == 5 && PHP_MINOR_VERSION >= 3) || (PHP_MAJOR_VERSION >= 6)
#undef	ZVAL_REFCOUNT
#undef	ZVAL_ADDREF
#undef	ZVAL_DELREF
#define	ZVAL_REFCOUNT	Z_REFCOUNT_P
#define	ZVAL_ADDREF		Z_ADDREF_P
#define	ZVAL_DELREF		Z_DELREF_P
#endif

/// hightman.20150614: for PHP7+
#if PHP_MAJOR_VERSION >= 7
#undef	FREE_ZVAL
#undef	ZEND_REGISTER_RESOURCE
#undef	RETVAL_RESOURCE
#undef	MAKE_STD_ZVAL
#undef	RETURN_STRING
#undef	add_assoc_string
#undef	add_assoc_stringl

#define FREE_ZVAL						efree_rel
#define ZEND_REGISTER_RESOURCE(x,y,z)	zend_register_resource(y,z)
#define RETVAL_RESOURCE					RETVAL_RES
#define MAKE_STD_ZVAL(x)				x = (zval *) emalloc(sizeof(zval))
#define RETURN_STRING(a,b)				RETVAL_STRING(a); return
#define add_assoc_string(a,b,c,d)		add_assoc_string_ex(a,b,strlen(b),c)
#define add_assoc_stringl(a,b,c,d,e)	add_assoc_stringl_ex(a,b,strlen(b),(char*)c,d)
#define ZEND_LIST_DELETE				zend_list_close

typedef size_t	str_size_t;
#else
#define ZEND_LIST_DELETE				zend_list_delete

typedef int str_size_t;
#endif

/// hightman.20220613: for PHP8
#ifndef TSRMLS_C
#define TSRMLS_C
#define TSRMLS_CC
#define TSRMLS_D
#endif

/// ZEND_DECLARE_MODULE_GLOBALS(scws)

static zend_class_entry *scws_class_entry_ptr;
static int le_scws;

#define PHP_SCWS_MODULE_VERSION		"0.2.4"
#define	PHP_SCWS_DEFAULT_CHARSET	"gbk"
#define	PHP_SCWS_OBJECT_TAG			"scws handler"
#define	DELREF_SCWS(x)	{	\
	if (x != NULL) {	\
		ZVAL_DELREF(x);		\
		if (ZVAL_REFCOUNT(x) <= 0) {	\
			zval_dtor(x);	\
			FREE_ZVAL(x);	\
		}	\
		x = NULL;	\
	}	\
}

#define	CHECK_DR_SCWS()	\
if (ps->s->r == NULL || ps->s->d == NULL) {	\
	char fpath[128], *folder, *ftoken;	\
	int plen;	\
	if (((folder = INI_STR("scws.default.fpath")) != NULL) \
		&& (plen = strlen(folder)) < 100)	\
	{	\
		strcpy(fpath, folder);	\
		ftoken = fpath + plen;	\
		if (plen > 0 && ftoken[-1] != DEFAULT_SLASH)	\
			*ftoken++ = DEFAULT_SLASH;	\
		if (ps->s->r == NULL) {	\
			if (ps->charset[0]	\
				&& strcmp(ps->charset, PHP_SCWS_DEFAULT_CHARSET))	\
			{	\
				zend_sprintf(ftoken, "rules.%s.ini", ps->charset);	\
			}	\
			else {	\
				strcpy(ftoken, "rules.ini");	\
			}	\
			scws_set_rule(ps->s, fpath);	\
		}	\
		if (ps->s->d == NULL) {	\
			if (ps->charset[0]	\
				&& strcmp(ps->charset, PHP_SCWS_DEFAULT_CHARSET))	\
			{	\
				zend_sprintf(ftoken, "dict.%s.xdb", ps->charset);	\
			}	\
			else	\
			{	\
				strcpy(ftoken, "dict.xdb");	\
			}	\
			scws_set_dict(ps->s, fpath,	SCWS_XDICT_XDB);	\
		}	\
	}	\
}

#if PHP_MAJOR_VERSION >= 7
#define	SCWS_FETCH_PARAMETERS(ts, ...)	\
	do {	\
		zval *tmp = getThis();	\
		if (tmp) {	\
			tmp = zend_hash_str_find(Z_OBJPROP_P(tmp), "handle", sizeof("handle") - 1);	\
			if (tmp == NULL) {	\
				php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unable to find the handle property");	\
				RETURN_FALSE;	\
			}	\
			if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, ts, ##__VA_ARGS__) == FAILURE) {	\
				return;	\
			}	\
		} else {	\
			if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r" ts, &tmp, ##__VA_ARGS__) == FAILURE) {	\
				return;	\
			}	\
		}	\
		ps = (struct php_scws *) zend_fetch_resource_ex(tmp, PHP_SCWS_OBJECT_TAG, le_scws);	\
	} while(0)
#else
#define	SCWS_FETCH_PARAMETERS(ts, ...)	\
	do {	\
		zval **tmp;	\
		zval *object = getThis();	\
		if (object) {	\
			if (zend_hash_find(Z_OBJPROP_P(object), "handle", sizeof("handle"), (void **)&tmp) == FAILURE) {	\
				php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unable to find the handle property");	\
				RETURN_FALSE;	\
			}	\
			if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, ts, ##__VA_ARGS__) == FAILURE) {	\
				return;	\
			}	\
		} else {	\
			if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r" ts, &object, ##__VA_ARGS__) == FAILURE) {	\
				return;	\
			}	\
			tmp = &object;	\
		}	\
		ZEND_FETCH_RESOURCE(ps, struct php_scws *, tmp, -1, PHP_SCWS_OBJECT_TAG, le_scws);	\
	} while(0)
#endif

struct php_scws
{
	scws_t s;
	zval *zt;
	char charset[8];
#if PHP_MAJOR_VERSION >= 7
	zend_resource *rsrc_id;
#else
	int rsrc_id;
#endif
};

#if (PHP_MAJOR_VERSION == 8 && PHP_MINOR_VERSION > 0) || (PHP_MAJOR_VERSION > 8)
#include "zend_attributes.h"
#include "scws_arginfo.h"
#else
zend_function_entry ext_functions[] = {
	PHP_FE(scws_open, NULL)
	PHP_FE(scws_new, NULL)
	PHP_FE(scws_close, NULL)
	PHP_FE(scws_set_charset, NULL)
	PHP_FE(scws_add_dict, NULL)
	PHP_FE(scws_set_dict, NULL)
	PHP_FE(scws_set_rule, NULL)
	PHP_FE(scws_set_ignore, NULL)
	PHP_FE(scws_set_multi, NULL)
	PHP_FE(scws_set_duality, NULL)
	PHP_FE(scws_send_text, NULL)
	PHP_FE(scws_get_result, NULL)
	PHP_FE(scws_get_tops, NULL)	
	PHP_FE(scws_has_word, NULL)
	PHP_FE(scws_get_words, NULL)
	PHP_FE(scws_version, NULL)
	{NULL, NULL, NULL}
};

static zend_function_entry class_SimpleCWS_methods[] = {
	PHP_FALIAS(close,		scws_close,			NULL)
	PHP_FALIAS(set_charset,	scws_set_charset,	NULL)
	PHP_FALIAS(add_dict,	scws_add_dict,		NULL)
	PHP_FALIAS(set_dict,	scws_set_dict,		NULL)
	PHP_FALIAS(set_rule,	scws_set_rule,		NULL)
	PHP_FALIAS(set_ignore,	scws_set_ignore,	NULL)
	PHP_FALIAS(set_multi,	scws_set_multi,		NULL)
	PHP_FALIAS(set_duality,	scws_set_duality,	NULL)
	PHP_FALIAS(send_text,	scws_send_text,		NULL)
	PHP_FALIAS(get_result,	scws_get_result,	NULL)
	PHP_FALIAS(get_tops,	scws_get_tops,		NULL)
	PHP_FALIAS(has_word,	scws_has_word,		NULL)
	PHP_FALIAS(get_words,	scws_get_words,		NULL)
	PHP_FALIAS(version,		scws_version,		NULL)
	{NULL, NULL, NULL}
};
#endif /* PHP_MAJOR_VERSION >= 8 */

static ZEND_RSRC_DTOR_FUNC(php_scws_dtor)
{
#if PHP_MAJOR_VERSION >= 7
#define rsrc	res
#endif
	if (rsrc->ptr) {
		struct php_scws *ps = (struct php_scws *) rsrc->ptr;
		scws_free(ps->s);
#if PHP_MAJOR_VERSION < 7
		DELREF_SCWS(ps->zt);
#endif
		efree(ps);
		rsrc->ptr = NULL;
	}
#if PHP_MAJOR_VERSION >= 7
#undef rsrc
#endif
}

zend_module_entry scws_module_entry = {
#if ZEND_MODULE_API_NO >= 20010901
	STANDARD_MODULE_HEADER,
#endif
	"scws",
	ext_functions,
	PHP_MINIT(scws),
	PHP_MSHUTDOWN(scws),
	NULL,
	PHP_RSHUTDOWN(scws),
	PHP_MINFO(scws),
#if ZEND_MODULE_API_NO >= 20010901
	PHP_SCWS_MODULE_VERSION,
#endif
	STANDARD_MODULE_PROPERTIES
};
/* }}} */

#ifdef COMPILE_DL_SCWS
ZEND_GET_MODULE(scws)
#endif

PHP_INI_BEGIN()
	PHP_INI_ENTRY("scws.default.charset", PHP_SCWS_DEFAULT_CHARSET, PHP_INI_ALL, NULL)
	PHP_INI_ENTRY("scws.default.fpath", NULL, PHP_INI_ALL, NULL)
PHP_INI_END()

PHP_MINIT_FUNCTION(scws)
{
	REGISTER_INI_ENTRIES();

	le_scws = zend_register_list_destructors_ex(php_scws_dtor, NULL, PHP_SCWS_OBJECT_TAG, module_number);
	scws_class_entry_ptr = register_class_SimpleCWS();

	REGISTER_LONG_CONSTANT("SCWS_XDICT_XDB",	SCWS_XDICT_XDB, CONST_CS|CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("SCWS_XDICT_MEM",	SCWS_XDICT_MEM, CONST_CS|CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("SCWS_XDICT_TXT",	SCWS_XDICT_TXT, CONST_CS|CONST_PERSISTENT);

	REGISTER_LONG_CONSTANT("SCWS_MULTI_NONE",	SCWS_MULTI_NONE, CONST_CS|CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("SCWS_MULTI_SHORT",	(SCWS_MULTI_SHORT>>12), CONST_CS|CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("SCWS_MULTI_DUALITY",(SCWS_MULTI_DUALITY>>12), CONST_CS|CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("SCWS_MULTI_ZMAIN",	(SCWS_MULTI_ZMAIN>>12), CONST_CS|CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("SCWS_MULTI_ZALL",	(SCWS_MULTI_ZALL>>12), CONST_CS|CONST_PERSISTENT);

	return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(scws)
{
	UNREGISTER_INI_ENTRIES();
	return SUCCESS;
}

PHP_RSHUTDOWN_FUNCTION(scws)
{
	return SUCCESS;
}

PHP_MINFO_FUNCTION(scws)
{
	php_info_print_table_start();
	php_info_print_table_header(2, "SCWS support", "Enabled");
	php_info_print_table_row(2, "SCWS Description", "Simple Chinese Words Segmentation");
	php_info_print_table_row(2, "PECL Module version", PHP_SCWS_MODULE_VERSION);
	php_info_print_table_row(2, "SCWS Library", SCWS_VERSION);
	php_info_print_table_row(2, "SCWS BugReport", SCWS_BUGREPORT);
	php_info_print_table_end();
	DISPLAY_INI_ENTRIES();
}

static void *_php_create_scws(TSRMLS_D)
{
	struct php_scws *ps;
	char *ini_cs;
	scws_t s;

	s = scws_new();
	if (s == NULL) {
		return NULL;	
	}

	ps = (struct php_scws *)emalloc(sizeof(struct php_scws));
	ps->s = s;
	ps->zt = NULL;
	ps->charset[0] = '\0';
	ps->rsrc_id = ZEND_REGISTER_RESOURCE(NULL, ps, le_scws);

	ini_cs = INI_STR("scws.default.charset");
	if (ini_cs != NULL && *ini_cs) {	
		memset(ps->charset, 0, sizeof(ps->charset));
		strncpy(ps->charset, ini_cs, sizeof(ps->charset)-1);
		scws_set_charset(s, ps->charset);
	}

	return ((void *)ps);
}

PHP_FUNCTION(scws_open)
{
	struct php_scws *ps;

	ps = (struct php_scws *)_php_create_scws(TSRMLS_C);
	if (ps == NULL) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Can't create new scws handler");
		RETURN_FALSE;
	}

	RETVAL_RESOURCE(ps->rsrc_id);
}

PHP_FUNCTION(scws_new)
{
	struct php_scws *ps;

	ps = (struct php_scws *)_php_create_scws(TSRMLS_C);
	if (ps == NULL) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Can't create new scws object");
		RETURN_FALSE;
	}

	object_init_ex(return_value, scws_class_entry_ptr);
	add_property_resource(return_value, "handle", ps->rsrc_id);
}

PHP_FUNCTION(scws_close)
{
	struct php_scws *ps;

	SCWS_FETCH_PARAMETERS("");

	ZEND_LIST_DELETE(ps->rsrc_id);
}

PHP_FUNCTION(scws_set_charset)
{
	char *cs;
	str_size_t cs_len;
	struct php_scws *ps;
	
	SCWS_FETCH_PARAMETERS("s", &cs, &cs_len);
	
	memset(ps->charset, 0, sizeof(ps->charset));
	strncpy(ps->charset, cs, sizeof(ps->charset)-1);
	scws_set_charset(ps->s, ps->charset);

	RETURN_TRUE;
}

PHP_FUNCTION(scws_add_dict)
{
	long xmode = 0;
	char *filepath, *fullpath = NULL;
	str_size_t filepath_len;
	struct php_scws *ps;

	SCWS_FETCH_PARAMETERS("s|l", &filepath, &filepath_len, &xmode);

	if (!(fullpath = expand_filepath(filepath, NULL TSRMLS_CC))) {
		RETURN_FALSE;
	}

#if PHP_API_VERSION < 20100412
	if (PG(safe_mode) && (!php_checkuid(fullpath, NULL, CHECKUID_CHECK_FILE_AND_DIR))) {
		efree(fullpath);
		RETURN_FALSE;
	}
#endif

	if (php_check_open_basedir(fullpath TSRMLS_CC)) {
		efree(fullpath);
		RETURN_FALSE;
	}

	xmode = (int) scws_add_dict(ps->s, fullpath, xmode);
	efree(fullpath);

	if (xmode != 0) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to add the dict file");
		RETURN_FALSE;
	}

	RETURN_TRUE;
}

PHP_FUNCTION(scws_set_dict)
{
	long xmode = 0;
	char *filepath, *fullpath = NULL;
	str_size_t filepath_len;
	struct php_scws *ps;

	SCWS_FETCH_PARAMETERS("s|l", &filepath, &filepath_len, &xmode);

	if (!(fullpath = expand_filepath(filepath, NULL TSRMLS_CC))) {
		RETURN_FALSE;
	}
	
#if PHP_API_VERSION < 20100412
	if (PG(safe_mode) && (!php_checkuid(fullpath, NULL, CHECKUID_CHECK_FILE_AND_DIR))) {
		efree(fullpath);
		RETURN_FALSE;
	}
#endif

	if (php_check_open_basedir(fullpath TSRMLS_CC)) {
		efree(fullpath);
		RETURN_FALSE;
	}

	xmode = (int) scws_set_dict(ps->s, fullpath, xmode);
	efree(fullpath);

	if (xmode != 0) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to set the dict file");
		RETURN_FALSE;
	}

	RETURN_TRUE;
}

PHP_FUNCTION(scws_set_rule)
{
	char *filepath, *fullpath = NULL;
	str_size_t filepath_len;
	struct php_scws *ps;

	SCWS_FETCH_PARAMETERS("s", &filepath, &filepath_len);

	if (!(fullpath = expand_filepath(filepath, NULL TSRMLS_CC))) {
		RETURN_FALSE;
	}
	
#if PHP_API_VERSION < 20100412
	if (PG(safe_mode) && (!php_checkuid(fullpath, NULL, CHECKUID_CHECK_FILE_AND_DIR))) {
		efree(fullpath);
		RETURN_FALSE;
	}
#endif

	if (php_check_open_basedir(fullpath TSRMLS_CC)) {
		efree(fullpath);
		RETURN_FALSE;
	}

	scws_set_rule(ps->s, fullpath);
	efree(fullpath);

	if (ps->s->r == NULL) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to load the ruleset file");
		RETURN_FALSE;
	}

	RETURN_TRUE;
}

PHP_FUNCTION(scws_set_ignore)
{
	zend_bool boolset = 1;
	struct php_scws *ps;

	SCWS_FETCH_PARAMETERS("b", &boolset);

	scws_set_ignore(ps->s, boolset ? SCWS_YEA : SCWS_NA);
	RETURN_TRUE;
}

PHP_FUNCTION(scws_set_multi)
{
	long multi = 0;
	struct php_scws *ps;

	SCWS_FETCH_PARAMETERS("l", &multi);

	if (multi < 0 || (multi & 0x10)) {
		RETURN_FALSE;
	}

	scws_set_multi(ps->s, (multi<<12));
	RETURN_TRUE;
}

PHP_FUNCTION(scws_set_duality)
{
	zend_bool boolset = 1;
	struct php_scws *ps;

	SCWS_FETCH_PARAMETERS("b", &boolset);

	scws_set_duality(ps->s, boolset ? SCWS_YEA : SCWS_NA);
	RETURN_TRUE;
}

PHP_FUNCTION(scws_send_text)
{
	zval *text;
	struct php_scws *ps;

	SCWS_FETCH_PARAMETERS("z", &text);

#if	PHP_MAJOR_VERSION >= 7
	convert_to_string_ex(text);
#else
	convert_to_string_ex(&text);
#endif

#if PHP_MAJOR_VERSION < 7
	DELREF_SCWS(ps->zt);
	ZVAL_ADDREF(text);
#endif
	ps->zt = text;

	scws_send_text(ps->s, Z_STRVAL_P(ps->zt), Z_STRLEN_P(ps->zt));

	CHECK_DR_SCWS();

	RETURN_TRUE;
}

PHP_FUNCTION(scws_get_result)
{
	zval *row;
	scws_res_t res, cur;
	struct php_scws *ps;

	SCWS_FETCH_PARAMETERS("");
	
	cur = res = scws_get_result(ps->s);
	if (res == NULL) {
		RETURN_FALSE;
	}
	
	array_init(return_value);
	while (cur != NULL) {
		MAKE_STD_ZVAL(row);
		array_init(row);
		add_assoc_stringl(row, "word", (char *) ps->s->txt + cur->off, cur->len, 1);
		add_assoc_long(row, "off", cur->off);
		add_assoc_long(row, "len", cur->len);
		add_assoc_double(row, "idf", (double) cur->idf);
		add_assoc_stringl(row, "attr", cur->attr, (cur->attr[1] == '\0' ? 1 : 2), 1);
		
		cur = cur->next;
		add_next_index_zval(return_value, row);		
#if PHP_MAJOR_VERSION >= 7
		efree(row);
#endif
	}
	scws_free_result(res);
}

// [, limit, [, exclude_attrs]]
PHP_FUNCTION(scws_get_tops)
{
	long limit = 0;
	char *attr = NULL;
	str_size_t attr_len;
	scws_top_t top, cur;
	zval *row;
	struct php_scws *ps;

	SCWS_FETCH_PARAMETERS("|ls", &limit, &attr, &attr_len);

	if (limit <= 0) {
		limit = 10;
	}
	cur = top = scws_get_tops(ps->s, limit, attr);
	array_init(return_value);
	while (cur != NULL) {
		MAKE_STD_ZVAL(row);
		array_init(row);

		add_assoc_string(row, "word", cur->word, 1);
		add_assoc_long(row, "times", cur->times);
		add_assoc_double(row, "weight", (double) cur->weight);
		add_assoc_stringl(row, "attr", cur->attr, (cur->attr[1] == '\0' ? 1 : 2), 1);

		cur = cur->next;
		add_next_index_zval(return_value, row);
#if PHP_MAJOR_VERSION >= 7
		efree(row);
#endif
	}
	scws_free_tops(top);
}

// <attr>
PHP_FUNCTION(scws_has_word)
{
	char *attr;
	str_size_t attr_len;
	struct php_scws *ps;

	SCWS_FETCH_PARAMETERS("s", &attr, &attr_len);

	if (scws_has_word(ps->s, attr) == 0) {
		RETURN_FALSE;
	}

	RETURN_TRUE;	
}

// <attr>
PHP_FUNCTION(scws_get_words)
{
	char *attr;
	str_size_t attr_len;
	scws_top_t top, cur;
	zval *row;
	struct php_scws *ps;

	SCWS_FETCH_PARAMETERS("s", &attr, &attr_len);

	array_init(return_value);
	cur = top = scws_get_words(ps->s, attr);
	while (cur != NULL) {
		MAKE_STD_ZVAL(row);
		array_init(row);
		add_assoc_string(row, "word", cur->word, 1);
		add_assoc_long(row, "times", cur->times);
		add_assoc_double(row, "weight", (double) cur->weight);
		add_assoc_stringl(row, "attr", cur->attr, (cur->attr[1] == '\0' ? 1 : 2), 1);

		cur = cur->next;
		add_next_index_zval(return_value, row);
#if PHP_MAJOR_VERSION >= 7
		efree(row);
#endif
	}
	scws_free_tops(top);
}

PHP_FUNCTION(scws_version)
{
	char buf[128];

	zend_sprintf(buf, "SCWS (Module version:%s, Library version:%s) - by hightman",
		PHP_SCWS_MODULE_VERSION, SCWS_VERSION);
	RETURN_STRING(buf, 1);
}

/// hightman.20150614: for PHP7+
#if PHP_MAJOR_VERSION >= 7
#undef	RETURN_STRING
#undef	add_assoc_string
#undef	add_assoc_stringl
#endif

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
