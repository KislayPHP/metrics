extern "C" {
#include "php.h"
#include "ext/standard/info.h"
#include "Zend/zend_API.h"
#include "Zend/zend_interfaces.h"
#include "Zend/zend_exceptions.h"
}

#include "php_kislayphp_metrics.h"

#include <cstring>
#include <pthread.h>
#include <string>
#include <unordered_map>

#ifndef zend_call_method_with_0_params
static inline void kislayphp_call_method_with_0_params(
    zend_object *obj,
    zend_class_entry *obj_ce,
    zend_function **fn_proxy,
    const char *function_name,
    zval *retval) {
    zend_call_method(obj, obj_ce, fn_proxy, function_name, std::strlen(function_name), retval, 0, nullptr, nullptr);
}

#define zend_call_method_with_0_params(obj, obj_ce, fn_proxy, function_name, retval) \
    kislayphp_call_method_with_0_params(obj, obj_ce, fn_proxy, function_name, retval)
#endif

#ifndef zend_call_method_with_1_params
static inline void kislayphp_call_method_with_1_params(
    zend_object *obj,
    zend_class_entry *obj_ce,
    zend_function **fn_proxy,
    const char *function_name,
    zval *retval,
    zval *param1) {
    zend_call_method(obj, obj_ce, fn_proxy, function_name, std::strlen(function_name), retval, 1, param1, nullptr);
}

#define zend_call_method_with_1_params(obj, obj_ce, fn_proxy, function_name, retval, param1) \
    kislayphp_call_method_with_1_params(obj, obj_ce, fn_proxy, function_name, retval, param1)
#endif

#ifndef zend_call_method_with_2_params
static inline void kislayphp_call_method_with_2_params(
    zend_object *obj,
    zend_class_entry *obj_ce,
    zend_function **fn_proxy,
    const char *function_name,
    zval *retval,
    zval *param1,
    zval *param2) {
    zend_call_method(obj, obj_ce, fn_proxy, function_name, std::strlen(function_name), retval, 2, param1, param2);
}

#define zend_call_method_with_2_params(obj, obj_ce, fn_proxy, function_name, retval, param1, param2) \
    kislayphp_call_method_with_2_params(obj, obj_ce, fn_proxy, function_name, retval, param1, param2)
#endif
static zend_class_entry *kislayphp_metrics_ce;
static zend_class_entry *kislayphp_metrics_client_ce;

typedef struct _php_kislayphp_metrics_t {
    std::unordered_map<std::string, zend_long> counters;
    pthread_mutex_t lock;
    zval client;
    bool has_client;
    zend_object std;
} php_kislayphp_metrics_t;

static zend_object_handlers kislayphp_metrics_handlers;

static inline php_kislayphp_metrics_t *php_kislayphp_metrics_from_obj(zend_object *obj) {
    return reinterpret_cast<php_kislayphp_metrics_t *>(
        reinterpret_cast<char *>(obj) - XtOffsetOf(php_kislayphp_metrics_t, std));
}

static zend_object *kislayphp_metrics_create_object(zend_class_entry *ce) {
    php_kislayphp_metrics_t *obj = static_cast<php_kislayphp_metrics_t *>(
        ecalloc(1, sizeof(php_kislayphp_metrics_t) + zend_object_properties_size(ce)));
    zend_object_std_init(&obj->std, ce);
    object_properties_init(&obj->std, ce);
    new (&obj->counters) std::unordered_map<std::string, zend_long>();
    pthread_mutex_init(&obj->lock, nullptr);
    ZVAL_UNDEF(&obj->client);
    obj->has_client = false;
    obj->std.handlers = &kislayphp_metrics_handlers;
    return &obj->std;
}

static void kislayphp_metrics_free_obj(zend_object *object) {
    php_kislayphp_metrics_t *obj = php_kislayphp_metrics_from_obj(object);
    if (obj->has_client) {
        zval_ptr_dtor(&obj->client);
    }
    obj->counters.~unordered_map();
    pthread_mutex_destroy(&obj->lock);
    zend_object_std_dtor(&obj->std);
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_metrics_void, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_metrics_inc, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, by, IS_LONG, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_metrics_get, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_metrics_set_client, 0, 0, 1)
    ZEND_ARG_OBJ_INFO(0, client, KislayPHP\\Metrics\\ClientInterface, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_metrics_reset, 0, 0, 0)
    ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 1)
ZEND_END_ARG_INFO()

PHP_METHOD(KislayPHPMetrics, __construct) {
    ZEND_PARSE_PARAMETERS_NONE();
}

PHP_METHOD(KislayPHPMetrics, setClient) {
    zval *client = nullptr;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_ZVAL(client)
    ZEND_PARSE_PARAMETERS_END();

    if (client == nullptr || Z_TYPE_P(client) != IS_OBJECT) {
        zend_throw_exception(zend_ce_exception, "Client must be an object", 0);
        RETURN_FALSE;
    }

    if (!instanceof_function(Z_OBJCE_P(client), kislayphp_metrics_client_ce)) {
        zend_throw_exception(zend_ce_exception, "Client must implement KislayPHP\\Metrics\\ClientInterface", 0);
        RETURN_FALSE;
    }

    php_kislayphp_metrics_t *obj = php_kislayphp_metrics_from_obj(Z_OBJ_P(getThis()));
    if (obj->has_client) {
        zval_ptr_dtor(&obj->client);
        obj->has_client = false;
    }
    ZVAL_COPY(&obj->client, client);
    obj->has_client = true;
    RETURN_TRUE;
}

PHP_METHOD(KislayPHPMetrics, inc) {
    char *name = nullptr;
    size_t name_len = 0;
    zend_long by = 1;
    ZEND_PARSE_PARAMETERS_START(1, 2)
        Z_PARAM_STRING(name, name_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_LONG(by)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_metrics_t *obj = php_kislayphp_metrics_from_obj(Z_OBJ_P(getThis()));
    if (obj->has_client) {
        zval name_zv;
        zval by_zv;
        ZVAL_STRINGL(&name_zv, name, name_len);
        ZVAL_LONG(&by_zv, by);

        zval retval;
        ZVAL_UNDEF(&retval);
        zend_call_method_with_2_params(Z_OBJ(obj->client), Z_OBJCE(obj->client), nullptr, "inc", &retval, &name_zv, &by_zv);

        zval_ptr_dtor(&name_zv);
        zval_ptr_dtor(&by_zv);

        if (Z_ISUNDEF(retval)) {
            RETURN_TRUE;
        }
        RETVAL_ZVAL(&retval, 1, 1);
        return;
    }

    pthread_mutex_lock(&obj->lock);
    obj->counters[std::string(name, name_len)] += by;
    pthread_mutex_unlock(&obj->lock);
    RETURN_TRUE;
}

PHP_METHOD(KislayPHPMetrics, get) {
    char *name = nullptr;
    size_t name_len = 0;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(name, name_len)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_metrics_t *obj = php_kislayphp_metrics_from_obj(Z_OBJ_P(getThis()));
    if (obj->has_client) {
        zval name_zv;
        ZVAL_STRINGL(&name_zv, name, name_len);

        zval retval;
        ZVAL_UNDEF(&retval);
        zend_call_method_with_1_params(Z_OBJ(obj->client), Z_OBJCE(obj->client), nullptr, "get", &retval, &name_zv);
        zval_ptr_dtor(&name_zv);

        if (Z_ISUNDEF(retval)) {
            RETURN_LONG(0);
        }
        RETVAL_ZVAL(&retval, 1, 1);
        return;
    }

    zend_long value = 0;
    bool found = false;
    pthread_mutex_lock(&obj->lock);
    auto it = obj->counters.find(std::string(name, name_len));
    if (it != obj->counters.end()) {
        value = it->second;
        found = true;
    }
    pthread_mutex_unlock(&obj->lock);
    if (!found) {
        RETURN_LONG(0);
    }
    RETURN_LONG(value);
}

PHP_METHOD(KislayPHPMetrics, all) {
    php_kislayphp_metrics_t *obj = php_kislayphp_metrics_from_obj(Z_OBJ_P(getThis()));
    if (obj->has_client) {
        zval retval;
        ZVAL_UNDEF(&retval);
        zend_call_method_with_0_params(Z_OBJ(obj->client), Z_OBJCE(obj->client), nullptr, "all", &retval);

        if (Z_ISUNDEF(retval)) {
            array_init(return_value);
            return;
        }
        RETVAL_ZVAL(&retval, 1, 1);
        return;
    }

    array_init(return_value);
    pthread_mutex_lock(&obj->lock);
    for (const auto &entry : obj->counters) {
        add_assoc_long(return_value, entry.first.c_str(), entry.second);
    }
    pthread_mutex_unlock(&obj->lock);
}

PHP_METHOD(KislayPHPMetrics, dec) {
    char *name = nullptr;
    size_t name_len = 0;
    zend_long by = 1;
    ZEND_PARSE_PARAMETERS_START(1, 2)
        Z_PARAM_STRING(name, name_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_LONG(by)
    ZEND_PARSE_PARAMETERS_END();

    if (by < 0) {
        by = -by;
    }

    php_kislayphp_metrics_t *obj = php_kislayphp_metrics_from_obj(Z_OBJ_P(getThis()));
    if (obj->has_client) {
        zval name_zv;
        zval by_zv;
        ZVAL_STRINGL(&name_zv, name, name_len);
        ZVAL_LONG(&by_zv, -by);

        zval retval;
        ZVAL_UNDEF(&retval);
        zend_call_method_with_2_params(Z_OBJ(obj->client), Z_OBJCE(obj->client), nullptr, "inc", &retval, &name_zv, &by_zv);

        zval_ptr_dtor(&name_zv);
        zval_ptr_dtor(&by_zv);

        if (Z_ISUNDEF(retval)) {
            RETURN_TRUE;
        }
        RETVAL_ZVAL(&retval, 1, 1);
        return;
    }

    pthread_mutex_lock(&obj->lock);
    obj->counters[std::string(name, name_len)] -= by;
    pthread_mutex_unlock(&obj->lock);
    RETURN_TRUE;
}

PHP_METHOD(KislayPHPMetrics, reset) {
    char *name = nullptr;
    size_t name_len = 0;
    bool has_name = false;
    ZEND_PARSE_PARAMETERS_START(0, 1)
        Z_PARAM_OPTIONAL
        Z_PARAM_STRING(name, name_len)
    ZEND_PARSE_PARAMETERS_END();
    if (ZEND_NUM_ARGS() == 1) {
        has_name = true;
    }

    php_kislayphp_metrics_t *obj = php_kislayphp_metrics_from_obj(Z_OBJ_P(getThis()));
    if (obj->has_client && has_name) {
        zval name_zv;
        zval zero_zv;
        ZVAL_STRINGL(&name_zv, name, name_len);
        ZVAL_LONG(&zero_zv, 0);

        zval retval;
        ZVAL_UNDEF(&retval);
        zend_call_method_with_2_params(Z_OBJ(obj->client), Z_OBJCE(obj->client), nullptr, "inc", &retval, &name_zv, &zero_zv);

        zval_ptr_dtor(&name_zv);
        zval_ptr_dtor(&zero_zv);
        if (!Z_ISUNDEF(retval)) {
            zval_ptr_dtor(&retval);
        }
        RETURN_TRUE;
    }

    pthread_mutex_lock(&obj->lock);
    if (has_name) {
        obj->counters[std::string(name, name_len)] = 0;
    } else {
        obj->counters.clear();
    }
    pthread_mutex_unlock(&obj->lock);
    RETURN_TRUE;
}

static const zend_function_entry kislayphp_metrics_methods[] = {
    PHP_ME(KislayPHPMetrics, __construct, arginfo_kislayphp_metrics_void, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPMetrics, setClient, arginfo_kislayphp_metrics_set_client, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPMetrics, inc, arginfo_kislayphp_metrics_inc, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPMetrics, dec, arginfo_kislayphp_metrics_inc, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPMetrics, get, arginfo_kislayphp_metrics_get, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPMetrics, all, arginfo_kislayphp_metrics_void, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPMetrics, reset, arginfo_kislayphp_metrics_reset, ZEND_ACC_PUBLIC)
    PHP_FE_END
};

static const zend_function_entry kislayphp_metrics_client_methods[] = {
    ZEND_ABSTRACT_ME(KislayPHPMetricsClientInterface, inc, arginfo_kislayphp_metrics_inc)
    ZEND_ABSTRACT_ME(KislayPHPMetricsClientInterface, get, arginfo_kislayphp_metrics_get)
    ZEND_ABSTRACT_ME(KislayPHPMetricsClientInterface, all, arginfo_kislayphp_metrics_void)
    PHP_FE_END
};

PHP_MINIT_FUNCTION(kislayphp_metrics) {
    zend_class_entry ce;
    INIT_NS_CLASS_ENTRY(ce, "KislayPHP\\Metrics", "ClientInterface", kislayphp_metrics_client_methods);
    kislayphp_metrics_client_ce = zend_register_internal_interface(&ce);
    INIT_NS_CLASS_ENTRY(ce, "KislayPHP\\Metrics", "Metrics", kislayphp_metrics_methods);
    kislayphp_metrics_ce = zend_register_internal_class(&ce);
    kislayphp_metrics_ce->create_object = kislayphp_metrics_create_object;
    std::memcpy(&kislayphp_metrics_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
    kislayphp_metrics_handlers.offset = XtOffsetOf(php_kislayphp_metrics_t, std);
    kislayphp_metrics_handlers.free_obj = kislayphp_metrics_free_obj;
    return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(kislayphp_metrics) {
    return SUCCESS;
}

PHP_MINFO_FUNCTION(kislayphp_metrics) {
    php_info_print_table_start();
    php_info_print_table_header(2, "kislayphp_metrics support", "enabled");
    php_info_print_table_row(2, "Version", PHP_KISLAYPHP_METRICS_VERSION);
    php_info_print_table_end();
}

zend_module_entry kislayphp_metrics_module_entry = {
    STANDARD_MODULE_HEADER,
    PHP_KISLAYPHP_METRICS_EXTNAME,
    nullptr,
    PHP_MINIT(kislayphp_metrics),
    PHP_MSHUTDOWN(kislayphp_metrics),
    nullptr,
    nullptr,
    PHP_MINFO(kislayphp_metrics),
    PHP_KISLAYPHP_METRICS_VERSION,
    STANDARD_MODULE_PROPERTIES
};

#if defined(COMPILE_DL_KISLAYPHP_METRICS) || defined(ZEND_COMPILE_DL_EXT)
#ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE();
#endif
extern "C" {
ZEND_GET_MODULE(kislayphp_metrics)
}
#endif
