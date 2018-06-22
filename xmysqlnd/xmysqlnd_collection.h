/*
  +----------------------------------------------------------------------+
  | PHP Version 7                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 2006-2018 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Authors: Andrey Hristov <andrey@php.net>                             |
  +----------------------------------------------------------------------+
*/
#ifndef XMYSQLND_COLLECTION_H
#define XMYSQLND_COLLECTION_H

#include "xmysqlnd_driver.h"
#include "xmysqlnd_crud_collection_commands.h"
#include "util/allocator.h"

namespace mysqlx {

namespace drv {

struct xmysqlnd_schema;
struct st_xmysqlnd_session_on_error_bind;

struct xmysqlnd_collection : public util::custom_allocable
{
public:
	xmysqlnd_collection() = default;
	xmysqlnd_collection(xmysqlnd_schema * const cur_schema,
								const MYSQLND_CSTRING cur_collection_name,
								zend_bool is_persistent);
	enum_func_status		exists_in_database(struct st_xmysqlnd_session_on_error_bind on_error, zval* exists);
	enum_func_status		count( struct st_xmysqlnd_session_on_error_bind on_error, zval* counter);
	xmysqlnd_stmt*       add(XMYSQLND_CRUD_COLLECTION_OP__ADD * crud_op);
	xmysqlnd_stmt*		remove(XMYSQLND_CRUD_COLLECTION_OP__REMOVE * op);
	xmysqlnd_stmt*		modify(XMYSQLND_CRUD_COLLECTION_OP__MODIFY * op);
	xmysqlnd_stmt*		find(XMYSQLND_CRUD_COLLECTION_OP__FIND * op);

	xmysqlnd_collection *   get_reference();
	enum_func_status		free_reference(MYSQLND_STATS * stats, MYSQLND_ERROR_INFO * error_info);
	void					free_contents();
	void					cleanup(MYSQLND_STATS * stats, MYSQLND_ERROR_INFO * error_info);
	MYSQLND_STRING          get_name(){
		return collection_name;
	}
	xmysqlnd_schema*     get_schema(){
		return schema;
	}
private:
	xmysqlnd_schema*	schema;
	MYSQLND_STRING      collection_name;
	zend_bool           persistent;
	unsigned int	    refcount;
};

PHP_MYSQL_XDEVAPI_API xmysqlnd_collection * xmysqlnd_collection_create(xmysqlnd_schema* schema,
																  const MYSQLND_CSTRING collection_name,
																  const zend_bool persistent,
																  const MYSQLND_CLASS_METHODS_TYPE(xmysqlnd_object_factory) * const object_factory,
																  MYSQLND_STATS * const stats,
																  MYSQLND_ERROR_INFO * const error_info);

PHP_MYSQL_XDEVAPI_API void xmysqlnd_collection_free(xmysqlnd_collection * const schema, MYSQLND_STATS * stats, MYSQLND_ERROR_INFO * error_info);

} // namespace drv

} // namespace mysqlx

#endif /* XMYSQLND_COLLECTION_H */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
