extern "C" {
#include "php.h"
#include "ext/standard/info.h"
#include "Zend/zend_exceptions.h"
}

#include "php_kislayphp_metrics.h"

#include <string>
#include <unordered_map>

static zend_class_entry *kislayphp_metrics_ce;

typedef struct _php_kislayphp_metrics_t {
    zend_object std;
    std::unordered_map<std::string, zend_long> counters;
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
    obj->std.handlers = &kislayphp_metrics_handlers;
    return &obj->std;
}

static void kislayphp_metrics_free_obj(zend_object *object) {
    php_kislayphp_metrics_t *obj = php_kislayphp_metrics_from_obj(object);
    obj->counters.~unordered_map();
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

PHP_METHOD(KislayPHPMetrics, __construct) {
    ZEND_PARSE_PARAMETERS_NONE();
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
    obj->counters[std::string(name, name_len)] += by;
    RETURN_TRUE;
}

PHP_METHOD(KislayPHPMetrics, get) {
    char *name = nullptr;
    size_t name_len = 0;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(name, name_len)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_metrics_t *obj = php_kislayphp_metrics_from_obj(Z_OBJ_P(getThis()));
    auto it = obj->counters.find(std::string(name, name_len));
    if (it == obj->counters.end()) {
        RETURN_LONG(0);
    }
    RETURN_LONG(it->second);
}

PHP_METHOD(KislayPHPMetrics, all) {
    php_kislayphp_metrics_t *obj = php_kislayphp_metrics_from_obj(Z_OBJ_P(getThis()));
    array_init(return_value);
    for (const auto &entry : obj->counters) {
        add_assoc_long(return_value, entry.first.c_str(), entry.second);
    }
}

static const zend_function_entry kislayphp_metrics_methods[] = {
    PHP_ME(KislayPHPMetrics, __construct, arginfo_kislayphp_metrics_void, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPMetrics, inc, arginfo_kislayphp_metrics_inc, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPMetrics, get, arginfo_kislayphp_metrics_get, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPMetrics, all, arginfo_kislayphp_metrics_void, ZEND_ACC_PUBLIC)
    PHP_FE_END
};

PHP_MINIT_FUNCTION(kislayphp_metrics) {
    zend_class_entry ce;
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
