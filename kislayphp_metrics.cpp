extern "C" {
#include "php.h"
#include "ext/standard/info.h"
#include "Zend/zend_API.h"
#include "Zend/zend_interfaces.h"
#include "Zend/zend_exceptions.h"
}

#include "php_kislayphp_metrics.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef KISLAYPHP_RPC
#include <grpcpp/grpcpp.h>

#include "platform.grpc.pb.h"
#endif

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
static zend_class_entry *kislayphp_counter_ce;
static zend_class_entry *kislayphp_gauge_ce;
static zend_class_entry *kislayphp_histogram_ce;
static zend_class_entry *kislayphp_timer_ce;
static zend_class_entry *kislayphp_collector_ce;

static zend_long kislayphp_env_long(const char *name, zend_long fallback) {
    const char *value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return fallback;
    }
    return static_cast<zend_long>(std::strtoll(value, nullptr, 10));
}

static bool kislayphp_env_bool(const char *name, bool fallback) {
    const char *value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return fallback;
    }
    if (std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0 || std::strcmp(value, "TRUE") == 0) {
        return true;
    }
    if (std::strcmp(value, "0") == 0 || std::strcmp(value, "false") == 0 || std::strcmp(value, "FALSE") == 0) {
        return false;
    }
    return fallback;
}

static std::string kislayphp_env_string(const char *name, const std::string &fallback) {
    const char *value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return fallback;
    }
    return std::string(value);
}

#ifdef KISLAYPHP_RPC
static bool kislayphp_rpc_enabled() {
    return kislayphp_env_bool("KISLAY_RPC_ENABLED", false);
}

static zend_long kislayphp_rpc_timeout_ms() {
    zend_long timeout = kislayphp_env_long("KISLAY_RPC_TIMEOUT_MS", 200);
    return timeout > 0 ? timeout : 200;
}

static std::string kislayphp_rpc_platform_endpoint() {
    return kislayphp_env_string("KISLAY_RPC_PLATFORM_ENDPOINT", "127.0.0.1:9100");
}

static kislay::platform::v1::MetricsService::Stub *kislayphp_rpc_metrics_stub(const std::string &endpoint) {
    static std::mutex lock;
    static std::string cached_endpoint;
    static std::shared_ptr<grpc::Channel> channel;
    static std::unique_ptr<kislay::platform::v1::MetricsService::Stub> stub;
    std::lock_guard<std::mutex> guard(lock);
    if (!stub || cached_endpoint != endpoint) {
        channel = grpc::CreateChannel(endpoint, grpc::InsecureChannelCredentials());
        stub = kislay::platform::v1::MetricsService::NewStub(channel);
        cached_endpoint = endpoint;
    }
    return stub.get();
}

static bool kislayphp_rpc_metrics_inc(const std::string &name, zend_long by, std::string *error) {
    auto *stub = kislayphp_rpc_metrics_stub(kislayphp_rpc_platform_endpoint());
    if (!stub) {
        if (error) {
            *error = "RPC stub unavailable";
        }
        return false;
    }

    kislay::platform::v1::IncRequest request;
    request.set_name(name);
    request.set_by(static_cast<int64_t>(by));
    kislay::platform::v1::IncResponse response;
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(kislayphp_rpc_timeout_ms()));

    grpc::Status status = stub->Inc(&context, request, &response);
    if (!status.ok()) {
        if (error) {
            *error = status.error_message();
        }
        return false;
    }
    if (!response.ok()) {
        if (error) {
            *error = response.error();
        }
        return false;
    }
    return true;
}

static bool kislayphp_rpc_metrics_get(const std::string &name, zend_long *value, std::string *error) {
    auto *stub = kislayphp_rpc_metrics_stub(kislayphp_rpc_platform_endpoint());
    if (!stub) {
        if (error) {
            *error = "RPC stub unavailable";
        }
        return false;
    }

    kislay::platform::v1::GetMetricRequest request;
    request.set_name(name);
    kislay::platform::v1::GetMetricResponse response;
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(kislayphp_rpc_timeout_ms()));

    grpc::Status status = stub->GetMetric(&context, request, &response);
    if (!status.ok()) {
        if (error) {
            *error = status.error_message();
        }
        return false;
    }
    if (value) {
        *value = static_cast<zend_long>(response.value());
    }
    return true;
}

static bool kislayphp_rpc_metrics_all(zval *return_value, std::string *error) {
    auto *stub = kislayphp_rpc_metrics_stub(kislayphp_rpc_platform_endpoint());
    if (!stub) {
        if (error) {
            *error = "RPC stub unavailable";
        }
        return false;
    }

    kislay::platform::v1::AllMetricsRequest request;
    kislay::platform::v1::AllMetricsResponse response;
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(kislayphp_rpc_timeout_ms()));

    grpc::Status status = stub->AllMetrics(&context, request, &response);
    if (!status.ok()) {
        if (error) {
            *error = status.error_message();
        }
        return false;
    }

    array_init(return_value);
    for (const auto &item : response.items()) {
        add_assoc_long(return_value, item.name().c_str(), static_cast<zend_long>(item.value()));
    }
    return true;
}

static bool kislayphp_rpc_metrics_reset(const std::string &name, bool reset_all, std::string *error) {
    auto *stub = kislayphp_rpc_metrics_stub(kislayphp_rpc_platform_endpoint());
    if (!stub) {
        if (error) {
            *error = "RPC stub unavailable";
        }
        return false;
    }

    kislay::platform::v1::ResetRequest request;
    if (!name.empty()) {
        request.set_name(name);
    }
    request.set_reset_all(reset_all);
    kislay::platform::v1::ResetResponse response;
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(kislayphp_rpc_timeout_ms()));

    grpc::Status status = stub->Reset(&context, request, &response);
    if (!status.ok()) {
        if (error) {
            *error = status.error_message();
        }
        return false;
    }
    if (!response.ok()) {
        if (error) {
            *error = response.error();
        }
        return false;
    }
    return true;
}
#endif

typedef struct _php_kislayphp_metrics_t {
    std::unordered_map<std::string, zend_long> counters;
    std::mutex lock;  // replaces pthread_mutex_t for consistency
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
    new (&obj->lock) std::mutex();
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
    obj->lock.~mutex();
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
    ZEND_ARG_OBJ_INFO(0, client, Kislay\\Metrics\\ClientInterface, 0)
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
        zend_throw_exception(zend_ce_exception, "Client must implement Kislay\\Metrics\\ClientInterface", 0);
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

#ifdef KISLAYPHP_RPC
    if (kislayphp_rpc_enabled()) {
        std::string error;
        if (kislayphp_rpc_metrics_inc(std::string(name, name_len), by, &error)) {
            RETURN_TRUE;
        }
    }
#endif

    { std::lock_guard<std::mutex> _mg(obj->lock);
    obj->counters[std::string(name, name_len)] += by;
    } // unlock
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

#ifdef KISLAYPHP_RPC
    if (kislayphp_rpc_enabled()) {
        zend_long value = 0;
        std::string error;
        if (kislayphp_rpc_metrics_get(std::string(name, name_len), &value, &error)) {
            RETURN_LONG(value);
        }
    }
#endif

    zend_long value = 0;
    bool found = false;
    { std::lock_guard<std::mutex> _mg(obj->lock);
    auto it = obj->counters.find(std::string(name, name_len));
    if (it != obj->counters.end()) {
        value = it->second;
        found = true;
    }
    } // unlock
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

#ifdef KISLAYPHP_RPC
    if (kislayphp_rpc_enabled()) {
        std::string error;
        if (kislayphp_rpc_metrics_all(return_value, &error)) {
            return;
        }
    }
#endif

    array_init(return_value);
    { std::lock_guard<std::mutex> _mg(obj->lock);
    for (const auto &entry : obj->counters) {
        add_assoc_long(return_value, entry.first.c_str(), entry.second);
    }
    } // unlock
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
        ZVAL_LONG(&by_zv, by);  // positive — client dec() handles direction

        zval retval;
        ZVAL_UNDEF(&retval);
        zend_call_method_with_2_params(Z_OBJ(obj->client), Z_OBJCE(obj->client), nullptr, "dec", &retval, &name_zv, &by_zv);

        zval_ptr_dtor(&name_zv);
        zval_ptr_dtor(&by_zv);

        if (Z_ISUNDEF(retval)) {
            RETURN_TRUE;
        }
        RETVAL_ZVAL(&retval, 1, 1);
        return;
    }

#ifdef KISLAYPHP_RPC
    if (kislayphp_rpc_enabled()) {
        std::string error;
        if (kislayphp_rpc_metrics_inc(std::string(name, name_len), -by, &error)) {
            RETURN_TRUE;
        }
    }
#endif

    { std::lock_guard<std::mutex> _mg(obj->lock);
    obj->counters[std::string(name, name_len)] -= by;
    } // unlock
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
    if (obj->has_client) {
        // Call client's reset() method — works for both named and global reset
        zval retval;
        ZVAL_UNDEF(&retval);
        if (has_name) {
            zval name_zv;
            ZVAL_STRINGL(&name_zv, name, name_len);
            zend_call_method_with_1_params(Z_OBJ(obj->client), Z_OBJCE(obj->client), nullptr, "reset", &retval, &name_zv);
            zval_ptr_dtor(&name_zv);
        } else {
            zend_call_method_with_0_params(Z_OBJ(obj->client), Z_OBJCE(obj->client), nullptr, "reset", &retval);
        }
        if (!Z_ISUNDEF(retval)) {
            zval_ptr_dtor(&retval);
        }
        RETURN_TRUE;
    }

#ifdef KISLAYPHP_RPC
    if (kislayphp_rpc_enabled()) {
        std::string error;
        std::string name_value = has_name ? std::string(name, name_len) : std::string();
        if (kislayphp_rpc_metrics_reset(name_value, !has_name, &error)) {
            RETURN_TRUE;
        }
    }
#endif

    { std::lock_guard<std::mutex> _mg(obj->lock);
    if (has_name) {
        obj->counters[std::string(name, name_len)] = 0;
    } else {
        obj->counters.clear();
    }
    } // unlock
    RETURN_TRUE;
}

/* =======================================================================
 * Module-level global Metrics — used by Counter/Gauge when no Metrics
 * instance is supplied to their constructor.
 * ======================================================================= */

static zval kislayphp_global_metrics_zval;
static bool kislayphp_global_metrics_inited = false;

static zval *kislayphp_get_global_metrics(void) {
    if (!kislayphp_global_metrics_inited) {
        object_init_ex(&kislayphp_global_metrics_zval, kislayphp_metrics_ce);
        kislayphp_global_metrics_inited = true;
    }
    return &kislayphp_global_metrics_zval;
}

static const std::vector<double> kislayphp_default_histogram_buckets = {
    0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0, 2.5, 5.0, 10.0
};

/* =======================================================================
 * Counter
 * ======================================================================= */

typedef struct _php_kislayphp_counter_t {
    std::string name;
    zval        metrics_zv;
    bool        has_metrics;
    zend_object std;
} php_kislayphp_counter_t;

static zend_object_handlers kislayphp_counter_handlers;

static inline php_kislayphp_counter_t *php_kislayphp_counter_from_obj(zend_object *obj) {
    return reinterpret_cast<php_kislayphp_counter_t *>(
        reinterpret_cast<char *>(obj) - XtOffsetOf(php_kislayphp_counter_t, std));
}

static zend_object *kislayphp_counter_create_object(zend_class_entry *ce) {
    php_kislayphp_counter_t *obj = static_cast<php_kislayphp_counter_t *>(
        ecalloc(1, sizeof(php_kislayphp_counter_t) + zend_object_properties_size(ce)));
    zend_object_std_init(&obj->std, ce);
    object_properties_init(&obj->std, ce);
    new (&obj->name) std::string();
    ZVAL_UNDEF(&obj->metrics_zv);
    obj->has_metrics = false;
    obj->std.handlers = &kislayphp_counter_handlers;
    return &obj->std;
}

static void kislayphp_counter_free_obj(zend_object *object) {
    php_kislayphp_counter_t *obj = php_kislayphp_counter_from_obj(object);
    if (obj->has_metrics) {
        zval_ptr_dtor(&obj->metrics_zv);
    }
    obj->name.~basic_string();
    zend_object_std_dtor(&obj->std);
}

/* Counter::__construct(string $name, ?Metrics $metrics = null) */
ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_counter_construct, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 0)
    ZEND_ARG_OBJ_INFO(0, metrics, Kislay\\Metrics\\Metrics, 1)
ZEND_END_ARG_INFO()

/* Counter::increment/decrement(int $by = 1): void */
ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_counter_delta, 0, 0, 0)
    ZEND_ARG_TYPE_INFO(0, by, IS_LONG, 1)
ZEND_END_ARG_INFO()

PHP_METHOD(KislayPHPCounter, __construct) {
    char *name = nullptr;
    size_t name_len = 0;
    zval *metrics_zv = nullptr;
    ZEND_PARSE_PARAMETERS_START(1, 2)
        Z_PARAM_STRING(name, name_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_ZVAL(metrics_zv)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_counter_t *obj = php_kislayphp_counter_from_obj(Z_OBJ_P(getThis()));
    obj->name = std::string(name, name_len);
    if (metrics_zv != nullptr && Z_TYPE_P(metrics_zv) == IS_OBJECT
            && instanceof_function(Z_OBJCE_P(metrics_zv), kislayphp_metrics_ce)) {
        ZVAL_COPY(&obj->metrics_zv, metrics_zv);
        obj->has_metrics = true;
    }
}

PHP_METHOD(KislayPHPCounter, increment) {
    zend_long by = 1;
    ZEND_PARSE_PARAMETERS_START(0, 1)
        Z_PARAM_OPTIONAL
        Z_PARAM_LONG(by)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_counter_t *obj = php_kislayphp_counter_from_obj(Z_OBJ_P(getThis()));
    zval *m = obj->has_metrics ? &obj->metrics_zv : kislayphp_get_global_metrics();
    zval name_zv, by_zv, retval;
    ZVAL_STRING(&name_zv, obj->name.c_str());
    ZVAL_LONG(&by_zv, by);
    ZVAL_UNDEF(&retval);
    zend_call_method_with_2_params(Z_OBJ_P(m), Z_OBJCE_P(m), nullptr, "inc", &retval, &name_zv, &by_zv);
    zval_ptr_dtor(&name_zv);
    zval_ptr_dtor(&by_zv);
    if (!Z_ISUNDEF(retval)) { zval_ptr_dtor(&retval); }
}

PHP_METHOD(KislayPHPCounter, decrement) {
    zend_long by = 1;
    ZEND_PARSE_PARAMETERS_START(0, 1)
        Z_PARAM_OPTIONAL
        Z_PARAM_LONG(by)
    ZEND_PARSE_PARAMETERS_END();

    if (by < 0) { by = -by; }
    php_kislayphp_counter_t *obj = php_kislayphp_counter_from_obj(Z_OBJ_P(getThis()));
    zval *m = obj->has_metrics ? &obj->metrics_zv : kislayphp_get_global_metrics();
    zval name_zv, by_zv, retval;
    ZVAL_STRING(&name_zv, obj->name.c_str());
    ZVAL_LONG(&by_zv, by);
    ZVAL_UNDEF(&retval);
    zend_call_method_with_2_params(Z_OBJ_P(m), Z_OBJCE_P(m), nullptr, "dec", &retval, &name_zv, &by_zv);
    zval_ptr_dtor(&name_zv);
    zval_ptr_dtor(&by_zv);
    if (!Z_ISUNDEF(retval)) { zval_ptr_dtor(&retval); }
}

PHP_METHOD(KislayPHPCounter, get) {
    ZEND_PARSE_PARAMETERS_NONE();
    php_kislayphp_counter_t *obj = php_kislayphp_counter_from_obj(Z_OBJ_P(getThis()));
    zval *m = obj->has_metrics ? &obj->metrics_zv : kislayphp_get_global_metrics();
    zval name_zv, retval;
    ZVAL_STRING(&name_zv, obj->name.c_str());
    ZVAL_UNDEF(&retval);
    zend_call_method_with_1_params(Z_OBJ_P(m), Z_OBJCE_P(m), nullptr, "get", &retval, &name_zv);
    zval_ptr_dtor(&name_zv);
    if (Z_ISUNDEF(retval)) { RETURN_LONG(0); }
    RETVAL_ZVAL(&retval, 1, 1);
}

PHP_METHOD(KislayPHPCounter, reset) {
    ZEND_PARSE_PARAMETERS_NONE();
    php_kislayphp_counter_t *obj = php_kislayphp_counter_from_obj(Z_OBJ_P(getThis()));
    zval *m = obj->has_metrics ? &obj->metrics_zv : kislayphp_get_global_metrics();
    zval name_zv, retval;
    ZVAL_STRING(&name_zv, obj->name.c_str());
    ZVAL_UNDEF(&retval);
    zend_call_method_with_1_params(Z_OBJ_P(m), Z_OBJCE_P(m), nullptr, "reset", &retval, &name_zv);
    zval_ptr_dtor(&name_zv);
    if (!Z_ISUNDEF(retval)) { zval_ptr_dtor(&retval); }
}

static const zend_function_entry kislayphp_counter_methods[] = {
    PHP_ME(KislayPHPCounter, __construct, arginfo_kislayphp_counter_construct, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPCounter, increment,   arginfo_kislayphp_counter_delta,     ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPCounter, decrement,   arginfo_kislayphp_counter_delta,     ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPCounter, get,         arginfo_kislayphp_metrics_void,      ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPCounter, reset,       arginfo_kislayphp_metrics_void,      ZEND_ACC_PUBLIC)
    PHP_FE_END
};

/* =======================================================================
 * Gauge
 * ======================================================================= */

typedef struct _php_kislayphp_gauge_t {
    std::string name;
    zval        metrics_zv;
    bool        has_metrics;
    // Metrics::reset()+inc() (see set() below) are two separate calls into
    // the backing Metrics object, each independently locked there but not
    // atomic as a pair - without this, two concurrent set() calls on the
    // same Gauge can interleave (both reset to 0, then both inc, landing on
    // the sum of both values instead of either intended one). Collector
    // caches one Gauge object per name and hands the same instance to every
    // caller, so serializing here covers the common case; it does not
    // protect a Gauge from racing a direct $metrics->inc()/reset() call
    // that bypasses this wrapper entirely.
    std::mutex  set_lock;
    zend_object std;
} php_kislayphp_gauge_t;

static zend_object_handlers kislayphp_gauge_handlers;

static inline php_kislayphp_gauge_t *php_kislayphp_gauge_from_obj(zend_object *obj) {
    return reinterpret_cast<php_kislayphp_gauge_t *>(
        reinterpret_cast<char *>(obj) - XtOffsetOf(php_kislayphp_gauge_t, std));
}

static zend_object *kislayphp_gauge_create_object(zend_class_entry *ce) {
    php_kislayphp_gauge_t *obj = static_cast<php_kislayphp_gauge_t *>(
        ecalloc(1, sizeof(php_kislayphp_gauge_t) + zend_object_properties_size(ce)));
    zend_object_std_init(&obj->std, ce);
    object_properties_init(&obj->std, ce);
    new (&obj->name) std::string();
    new (&obj->set_lock) std::mutex();
    ZVAL_UNDEF(&obj->metrics_zv);
    obj->has_metrics = false;
    obj->std.handlers = &kislayphp_gauge_handlers;
    return &obj->std;
}

static void kislayphp_gauge_free_obj(zend_object *object) {
    php_kislayphp_gauge_t *obj = php_kislayphp_gauge_from_obj(object);
    if (obj->has_metrics) {
        zval_ptr_dtor(&obj->metrics_zv);
    }
    obj->name.~basic_string();
    obj->set_lock.~mutex();
    zend_object_std_dtor(&obj->std);
}

/* Gauge::__construct(string $name, ?Metrics $metrics = null) */
ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_gauge_construct, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 0)
    ZEND_ARG_OBJ_INFO(0, metrics, Kislay\\Metrics\\Metrics, 1)
ZEND_END_ARG_INFO()

/* Gauge::set(int $value): void */
ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_gauge_set, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, value, IS_LONG, 0)
ZEND_END_ARG_INFO()

/* Gauge::increment/decrement(int $by = 1): void */
ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_gauge_delta, 0, 0, 0)
    ZEND_ARG_TYPE_INFO(0, by, IS_LONG, 1)
ZEND_END_ARG_INFO()

PHP_METHOD(KislayPHPGauge, __construct) {
    char *name = nullptr;
    size_t name_len = 0;
    zval *metrics_zv = nullptr;
    ZEND_PARSE_PARAMETERS_START(1, 2)
        Z_PARAM_STRING(name, name_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_ZVAL(metrics_zv)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_gauge_t *obj = php_kislayphp_gauge_from_obj(Z_OBJ_P(getThis()));
    obj->name = std::string(name, name_len);
    if (metrics_zv != nullptr && Z_TYPE_P(metrics_zv) == IS_OBJECT
            && instanceof_function(Z_OBJCE_P(metrics_zv), kislayphp_metrics_ce)) {
        ZVAL_COPY(&obj->metrics_zv, metrics_zv);
        obj->has_metrics = true;
    }
}

PHP_METHOD(KislayPHPGauge, set) {
    zend_long value;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_LONG(value)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_gauge_t *obj = php_kislayphp_gauge_from_obj(Z_OBJ_P(getThis()));
    zval *m = obj->has_metrics ? &obj->metrics_zv : kislayphp_get_global_metrics();
    /* set = reset then inc by value - serialized against concurrent set()
     * calls on this same Gauge instance so the two-step composite can't
     * interleave (see set_lock's field comment). */
    std::lock_guard<std::mutex> _mg(obj->set_lock);
    zval name_zv, retval;
    ZVAL_STRING(&name_zv, obj->name.c_str());
    ZVAL_UNDEF(&retval);
    zend_call_method_with_1_params(Z_OBJ_P(m), Z_OBJCE_P(m), nullptr, "reset", &retval, &name_zv);
    if (!Z_ISUNDEF(retval)) { zval_ptr_dtor(&retval); ZVAL_UNDEF(&retval); }
    if (value != 0) {
        zval by_zv;
        ZVAL_LONG(&by_zv, value);
        zend_call_method_with_2_params(Z_OBJ_P(m), Z_OBJCE_P(m), nullptr, "inc", &retval, &name_zv, &by_zv);
        zval_ptr_dtor(&by_zv);
        if (!Z_ISUNDEF(retval)) { zval_ptr_dtor(&retval); }
    }
    zval_ptr_dtor(&name_zv);
}

PHP_METHOD(KislayPHPGauge, increment) {
    zend_long by = 1;
    ZEND_PARSE_PARAMETERS_START(0, 1)
        Z_PARAM_OPTIONAL
        Z_PARAM_LONG(by)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_gauge_t *obj = php_kislayphp_gauge_from_obj(Z_OBJ_P(getThis()));
    zval *m = obj->has_metrics ? &obj->metrics_zv : kislayphp_get_global_metrics();
    zval name_zv, by_zv, retval;
    ZVAL_STRING(&name_zv, obj->name.c_str());
    ZVAL_LONG(&by_zv, by);
    ZVAL_UNDEF(&retval);
    zend_call_method_with_2_params(Z_OBJ_P(m), Z_OBJCE_P(m), nullptr, "inc", &retval, &name_zv, &by_zv);
    zval_ptr_dtor(&name_zv);
    zval_ptr_dtor(&by_zv);
    if (!Z_ISUNDEF(retval)) { zval_ptr_dtor(&retval); }
}

PHP_METHOD(KislayPHPGauge, decrement) {
    zend_long by = 1;
    ZEND_PARSE_PARAMETERS_START(0, 1)
        Z_PARAM_OPTIONAL
        Z_PARAM_LONG(by)
    ZEND_PARSE_PARAMETERS_END();

    if (by < 0) { by = -by; }
    php_kislayphp_gauge_t *obj = php_kislayphp_gauge_from_obj(Z_OBJ_P(getThis()));
    zval *m = obj->has_metrics ? &obj->metrics_zv : kislayphp_get_global_metrics();
    zval name_zv, by_zv, retval;
    ZVAL_STRING(&name_zv, obj->name.c_str());
    ZVAL_LONG(&by_zv, by);
    ZVAL_UNDEF(&retval);
    zend_call_method_with_2_params(Z_OBJ_P(m), Z_OBJCE_P(m), nullptr, "dec", &retval, &name_zv, &by_zv);
    zval_ptr_dtor(&name_zv);
    zval_ptr_dtor(&by_zv);
    if (!Z_ISUNDEF(retval)) { zval_ptr_dtor(&retval); }
}

PHP_METHOD(KislayPHPGauge, get) {
    ZEND_PARSE_PARAMETERS_NONE();
    php_kislayphp_gauge_t *obj = php_kislayphp_gauge_from_obj(Z_OBJ_P(getThis()));
    zval *m = obj->has_metrics ? &obj->metrics_zv : kislayphp_get_global_metrics();
    zval name_zv, retval;
    ZVAL_STRING(&name_zv, obj->name.c_str());
    ZVAL_UNDEF(&retval);
    zend_call_method_with_1_params(Z_OBJ_P(m), Z_OBJCE_P(m), nullptr, "get", &retval, &name_zv);
    zval_ptr_dtor(&name_zv);
    if (Z_ISUNDEF(retval)) { RETURN_LONG(0); }
    RETVAL_ZVAL(&retval, 1, 1);
}

static const zend_function_entry kislayphp_gauge_methods[] = {
    PHP_ME(KislayPHPGauge, __construct, arginfo_kislayphp_gauge_construct, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPGauge, set,         arginfo_kislayphp_gauge_set,       ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPGauge, increment,   arginfo_kislayphp_gauge_delta,     ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPGauge, decrement,   arginfo_kislayphp_gauge_delta,     ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPGauge, get,         arginfo_kislayphp_metrics_void,    ZEND_ACC_PUBLIC)
    PHP_FE_END
};

/* =======================================================================
 * Histogram
 * ======================================================================= */

typedef struct _php_kislayphp_histogram_t {
    std::string           name;
    std::vector<double>   buckets;       // sorted bucket upper bounds ("le")
    // Per-bucket, non-cumulative observation counts, same size/order as
    // buckets (bucket_counts[i] = count of observations whose bucket is
    // buckets[i], found via lower_bound - NOT the cumulative "le" count
    // Prometheus exposes; export() sums these into a running total instead
    // of storing every raw observation, which is what O(1)-space requires).
    std::vector<uint64_t> bucket_counts;
    uint64_t              count;         // total observations (incl. +Inf overflow)
    double                sum;
    std::mutex            lock;
    zend_object           std;
} php_kislayphp_histogram_t;

static zend_object_handlers kislayphp_histogram_handlers;

static inline php_kislayphp_histogram_t *php_kislayphp_histogram_from_obj(zend_object *obj) {
    return reinterpret_cast<php_kislayphp_histogram_t *>(
        reinterpret_cast<char *>(obj) - XtOffsetOf(php_kislayphp_histogram_t, std));
}

static zend_object *kislayphp_histogram_create_object(zend_class_entry *ce) {
    php_kislayphp_histogram_t *obj = static_cast<php_kislayphp_histogram_t *>(
        ecalloc(1, sizeof(php_kislayphp_histogram_t) + zend_object_properties_size(ce)));
    zend_object_std_init(&obj->std, ce);
    object_properties_init(&obj->std, ce);
    new (&obj->name)          std::string();
    new (&obj->buckets)       std::vector<double>(kislayphp_default_histogram_buckets);
    new (&obj->bucket_counts) std::vector<uint64_t>(kislayphp_default_histogram_buckets.size(), 0);
    obj->count = 0;
    obj->sum   = 0.0;
    new (&obj->lock)          std::mutex();
    obj->std.handlers = &kislayphp_histogram_handlers;
    return &obj->std;
}

static void kislayphp_histogram_free_obj(zend_object *object) {
    php_kislayphp_histogram_t *obj = php_kislayphp_histogram_from_obj(object);
    obj->name.~basic_string();
    obj->buckets.~vector();
    obj->bucket_counts.~vector();
    obj->lock.~mutex();
    zend_object_std_dtor(&obj->std);
}

/* Histogram::__construct(string $name, array $buckets = []) */
ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_histogram_construct, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, buckets, IS_ARRAY, 1)
ZEND_END_ARG_INFO()

/* Histogram::observe(float $value): void */
ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_histogram_observe, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, value, IS_DOUBLE, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(KislayPHPHistogram, __construct) {
    char *name = nullptr;
    size_t name_len = 0;
    zval *buckets_zv = nullptr;
    ZEND_PARSE_PARAMETERS_START(1, 2)
        Z_PARAM_STRING(name, name_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_ARRAY_EX(buckets_zv, 1, 0)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_histogram_t *obj = php_kislayphp_histogram_from_obj(Z_OBJ_P(getThis()));
    obj->name = std::string(name, name_len);
    if (buckets_zv != nullptr && Z_TYPE_P(buckets_zv) == IS_ARRAY
            && zend_hash_num_elements(Z_ARRVAL_P(buckets_zv)) > 0) {
        obj->buckets.clear();
        zval *val;
        ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(buckets_zv), val) {
            obj->buckets.push_back(zval_get_double(val));
        } ZEND_HASH_FOREACH_END();
        std::sort(obj->buckets.begin(), obj->buckets.end());
        obj->bucket_counts.assign(obj->buckets.size(), 0);
    }
}

PHP_METHOD(KislayPHPHistogram, observe) {
    double value;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_DOUBLE(value)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_histogram_t *obj = php_kislayphp_histogram_from_obj(Z_OBJ_P(getThis()));
    { std::lock_guard<std::mutex> _mg(obj->lock);
    obj->sum += value;
    obj->count++;
    // First bucket whose upper bound is >= value ("le" semantics); a value
    // past every configured bucket only counts toward the +Inf total
    // (obj->count) with no specific bucket incremented - matches the
    // previous raw-storage behavior's "v <= bucket" comparison exactly,
    // just aggregated at observe() time instead of scanned at export() time.
    auto it = std::lower_bound(obj->buckets.begin(), obj->buckets.end(), value);
    if (it != obj->buckets.end()) {
        obj->bucket_counts[static_cast<size_t>(it - obj->buckets.begin())]++;
    }
    } // unlock
}

PHP_METHOD(KislayPHPHistogram, getCount) {
    ZEND_PARSE_PARAMETERS_NONE();
    php_kislayphp_histogram_t *obj = php_kislayphp_histogram_from_obj(Z_OBJ_P(getThis()));
    std::lock_guard<std::mutex> _mg(obj->lock);
    RETURN_LONG(static_cast<zend_long>(obj->count));
}

PHP_METHOD(KislayPHPHistogram, getSum) {
    ZEND_PARSE_PARAMETERS_NONE();
    php_kislayphp_histogram_t *obj = php_kislayphp_histogram_from_obj(Z_OBJ_P(getThis()));
    std::lock_guard<std::mutex> _mg(obj->lock);
    RETURN_DOUBLE(obj->sum);
}

PHP_METHOD(KislayPHPHistogram, getBuckets) {
    ZEND_PARSE_PARAMETERS_NONE();
    php_kislayphp_histogram_t *obj = php_kislayphp_histogram_from_obj(Z_OBJ_P(getThis()));
    array_init(return_value);
    std::lock_guard<std::mutex> _mg(obj->lock);
    for (double b : obj->buckets) {
        add_next_index_double(return_value, b);
    }
}

PHP_METHOD(KislayPHPHistogram, export) {
    ZEND_PARSE_PARAMETERS_NONE();
    php_kislayphp_histogram_t *obj = php_kislayphp_histogram_from_obj(Z_OBJ_P(getThis()));

    std::string output;
    std::lock_guard<std::mutex> _mg(obj->lock);

    const std::string &metric_name = obj->name;
    double sum = obj->sum;
    zend_long count = static_cast<zend_long>(obj->count);

    output += "# HELP " + metric_name + " Histogram: " + metric_name + "\n";
    output += "# TYPE " + metric_name + " histogram\n";

    // bucket_counts is per-bucket (non-cumulative); Prometheus "le" buckets
    // are cumulative, so accumulate on the way out - O(num buckets), not
    // O(observations), since num buckets stays small and fixed.
    uint64_t cumulative = 0;
    for (size_t i = 0; i < obj->buckets.size(); ++i) {
        cumulative += obj->bucket_counts[i];
        char le_buf[64], line_buf[256];
        snprintf(le_buf, sizeof(le_buf), "%g", obj->buckets[i]);
        snprintf(line_buf, sizeof(line_buf), "%s_bucket{le=\"%s\"} %llu\n",
                 metric_name.c_str(), le_buf, (unsigned long long)cumulative);
        output += line_buf;
    }

    { char line_buf[256];
    snprintf(line_buf, sizeof(line_buf), "%s_bucket{le=\"+Inf\"} %ld\n",
             metric_name.c_str(), (long)count);
    output += line_buf;
    }

    { char buf[256];
    snprintf(buf, sizeof(buf), "%s_count %ld\n", metric_name.c_str(), (long)count);
    output += buf;
    snprintf(buf, sizeof(buf), "%s_sum %f\n", metric_name.c_str(), sum);
    output += buf;
    }

    RETURN_STRING(output.c_str());
}

static const zend_function_entry kislayphp_histogram_methods[] = {
    PHP_ME(KislayPHPHistogram, __construct, arginfo_kislayphp_histogram_construct, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPHistogram, observe,     arginfo_kislayphp_histogram_observe,   ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPHistogram, getCount,    arginfo_kislayphp_metrics_void,        ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPHistogram, getSum,      arginfo_kislayphp_metrics_void,        ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPHistogram, getBuckets,  arginfo_kislayphp_metrics_void,        ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPHistogram, export,      arginfo_kislayphp_metrics_void,        ZEND_ACC_PUBLIC)
    PHP_FE_END
};

/* =======================================================================
 * Timer
 * ======================================================================= */

typedef struct _php_kislayphp_timer_t {
    std::string         name;
    zval                histogram_zv;
    bool                has_histogram;
    bool                running;
    std::chrono::steady_clock::time_point start_tp;
    std::mutex          lock;
    zend_object         std;
} php_kislayphp_timer_t;

static zend_object_handlers kislayphp_timer_handlers;

static inline php_kislayphp_timer_t *php_kislayphp_timer_from_obj(zend_object *obj) {
    return reinterpret_cast<php_kislayphp_timer_t *>(
        reinterpret_cast<char *>(obj) - XtOffsetOf(php_kislayphp_timer_t, std));
}

static zend_object *kislayphp_timer_create_object(zend_class_entry *ce) {
    php_kislayphp_timer_t *obj = static_cast<php_kislayphp_timer_t *>(
        ecalloc(1, sizeof(php_kislayphp_timer_t) + zend_object_properties_size(ce)));
    zend_object_std_init(&obj->std, ce);
    object_properties_init(&obj->std, ce);
    new (&obj->name)     std::string();
    new (&obj->lock)     std::mutex();
    ZVAL_UNDEF(&obj->histogram_zv);
    obj->has_histogram = false;
    obj->running = false;
    obj->std.handlers = &kislayphp_timer_handlers;
    return &obj->std;
}

static void kislayphp_timer_free_obj(zend_object *object) {
    php_kislayphp_timer_t *obj = php_kislayphp_timer_from_obj(object);
    if (obj->has_histogram) {
        zval_ptr_dtor(&obj->histogram_zv);
    }
    obj->name.~basic_string();
    obj->lock.~mutex();
    zend_object_std_dtor(&obj->std);
}

/* Timer::__construct(string $name, ?Histogram $histogram = null) */
ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_timer_construct, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 0)
    ZEND_ARG_OBJ_INFO(0, histogram, Kislay\\Metrics\\Histogram, 1)
ZEND_END_ARG_INFO()

/* Timer::record(float $ms): void */
ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_timer_record, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, ms, IS_DOUBLE, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(KislayPHPTimer, __construct) {
    char *name = nullptr;
    size_t name_len = 0;
    zval *histogram_zv = nullptr;
    ZEND_PARSE_PARAMETERS_START(1, 2)
        Z_PARAM_STRING(name, name_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_ZVAL(histogram_zv)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_timer_t *obj = php_kislayphp_timer_from_obj(Z_OBJ_P(getThis()));
    obj->name = std::string(name, name_len);
    if (histogram_zv != nullptr && Z_TYPE_P(histogram_zv) == IS_OBJECT
            && instanceof_function(Z_OBJCE_P(histogram_zv), kislayphp_histogram_ce)) {
        ZVAL_COPY(&obj->histogram_zv, histogram_zv);
        obj->has_histogram = true;
    }
}

PHP_METHOD(KislayPHPTimer, start) {
    ZEND_PARSE_PARAMETERS_NONE();
    php_kislayphp_timer_t *obj = php_kislayphp_timer_from_obj(Z_OBJ_P(getThis()));
    { std::lock_guard<std::mutex> _mg(obj->lock);
    obj->start_tp = std::chrono::steady_clock::now();
    obj->running  = true;
    } // unlock
}

PHP_METHOD(KislayPHPTimer, stop) {
    ZEND_PARSE_PARAMETERS_NONE();
    php_kislayphp_timer_t *obj = php_kislayphp_timer_from_obj(Z_OBJ_P(getThis()));

    double elapsed_ms;
    bool was_running;
    { std::lock_guard<std::mutex> _mg(obj->lock);
    was_running = obj->running;
    if (!was_running) {
        RETURN_DOUBLE(-1.0);
    }
    auto now = std::chrono::steady_clock::now();
    elapsed_ms = std::chrono::duration<double, std::milli>(now - obj->start_tp).count();
    obj->running = false;
    } // unlock

    if (obj->has_histogram) {
        zval val_zv, retval;
        ZVAL_DOUBLE(&val_zv, elapsed_ms);
        ZVAL_UNDEF(&retval);
        zend_call_method_with_1_params(Z_OBJ(obj->histogram_zv), Z_OBJCE(obj->histogram_zv),
                                       nullptr, "observe", &retval, &val_zv);
        zval_ptr_dtor(&val_zv);
        if (!Z_ISUNDEF(retval)) { zval_ptr_dtor(&retval); }
    }

    RETURN_DOUBLE(elapsed_ms);
}

PHP_METHOD(KislayPHPTimer, record) {
    double ms;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_DOUBLE(ms)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_timer_t *obj = php_kislayphp_timer_from_obj(Z_OBJ_P(getThis()));
    if (obj->has_histogram) {
        zval val_zv, retval;
        ZVAL_DOUBLE(&val_zv, ms);
        ZVAL_UNDEF(&retval);
        zend_call_method_with_1_params(Z_OBJ(obj->histogram_zv), Z_OBJCE(obj->histogram_zv),
                                       nullptr, "observe", &retval, &val_zv);
        zval_ptr_dtor(&val_zv);
        if (!Z_ISUNDEF(retval)) { zval_ptr_dtor(&retval); }
    }
    // Without a histogram there's nowhere for a recorded value to go - no
    // getter ever exposed the old records vector, so accumulating into it
    // was pure unbounded growth with no payoff. record() is only meaningful
    // when constructed with a Histogram; without one this is a no-op.
}

static const zend_function_entry kislayphp_timer_methods[] = {
    PHP_ME(KislayPHPTimer, __construct, arginfo_kislayphp_timer_construct, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPTimer, start,       arginfo_kislayphp_metrics_void,    ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPTimer, stop,        arginfo_kislayphp_metrics_void,    ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPTimer, record,      arginfo_kislayphp_timer_record,    ZEND_ACC_PUBLIC)
    PHP_FE_END
};

/* =======================================================================
 * Collector
 * ======================================================================= */

typedef struct _php_kislayphp_collector_t {
    std::string                           namespace_str;
    std::unordered_map<std::string, zval> counters;
    std::unordered_map<std::string, zval> gauges;
    std::unordered_map<std::string, zval> histograms;
    std::mutex                            lock;
    zend_object                           std;
} php_kislayphp_collector_t;

static zend_object_handlers kislayphp_collector_handlers;

static inline php_kislayphp_collector_t *php_kislayphp_collector_from_obj(zend_object *obj) {
    return reinterpret_cast<php_kislayphp_collector_t *>(
        reinterpret_cast<char *>(obj) - XtOffsetOf(php_kislayphp_collector_t, std));
}

static zend_object *kislayphp_collector_create_object(zend_class_entry *ce) {
    php_kislayphp_collector_t *obj = static_cast<php_kislayphp_collector_t *>(
        ecalloc(1, sizeof(php_kislayphp_collector_t) + zend_object_properties_size(ce)));
    zend_object_std_init(&obj->std, ce);
    object_properties_init(&obj->std, ce);
    new (&obj->namespace_str) std::string();
    new (&obj->counters)      std::unordered_map<std::string, zval>();
    new (&obj->gauges)        std::unordered_map<std::string, zval>();
    new (&obj->histograms)    std::unordered_map<std::string, zval>();
    new (&obj->lock)          std::mutex();
    obj->std.handlers = &kislayphp_collector_handlers;
    return &obj->std;
}

static void kislayphp_collector_free_obj(zend_object *object) {
    php_kislayphp_collector_t *obj = php_kislayphp_collector_from_obj(object);
    for (auto &kv : obj->counters)   { zval_ptr_dtor(&kv.second); }
    for (auto &kv : obj->gauges)     { zval_ptr_dtor(&kv.second); }
    for (auto &kv : obj->histograms) { zval_ptr_dtor(&kv.second); }
    obj->namespace_str.~basic_string();
    obj->counters.~unordered_map();
    obj->gauges.~unordered_map();
    obj->histograms.~unordered_map();
    obj->lock.~mutex();
    zend_object_std_dtor(&obj->std);
}

/* Collector::__construct(string $namespace = '') */
ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_collector_construct, 0, 0, 0)
    ZEND_ARG_TYPE_INFO(0, namespace, IS_STRING, 1)
ZEND_END_ARG_INFO()

/* Collector::counter/gauge(string $name): Counter/Gauge */
ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_collector_named, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 0)
ZEND_END_ARG_INFO()

/* Collector::histogram(string $name, array $buckets = []): Histogram */
ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_collector_histogram, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, buckets, IS_ARRAY, 1)
ZEND_END_ARG_INFO()

PHP_METHOD(KislayPHPCollector, __construct) {
    char *ns = nullptr;
    size_t ns_len = 0;
    ZEND_PARSE_PARAMETERS_START(0, 1)
        Z_PARAM_OPTIONAL
        Z_PARAM_STRING(ns, ns_len)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_collector_t *obj = php_kislayphp_collector_from_obj(Z_OBJ_P(getThis()));
    if (ns != nullptr && ns_len > 0) {
        obj->namespace_str = std::string(ns, ns_len);
        if (obj->namespace_str.back() != '_') {
            obj->namespace_str += '_';
        }
    }
}

PHP_METHOD(KislayPHPCollector, counter) {
    char *name = nullptr;
    size_t name_len = 0;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(name, name_len)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_collector_t *obj = php_kislayphp_collector_from_obj(Z_OBJ_P(getThis()));
    std::string key = obj->namespace_str + std::string(name, name_len);

    { std::lock_guard<std::mutex> _mg(obj->lock);
    auto it = obj->counters.find(key);
    if (it != obj->counters.end()) {
        RETURN_ZVAL(&it->second, 1, 0);
    }
    } // unlock — counter not found; create below, then re-lock to insert

    zval new_counter;
    object_init_ex(&new_counter, kislayphp_counter_ce);
    zval name_zv, retval;
    ZVAL_STRING(&name_zv, key.c_str());
    ZVAL_UNDEF(&retval);
    zend_call_method_with_1_params(Z_OBJ(new_counter), kislayphp_counter_ce,
                                   nullptr, "__construct", &retval, &name_zv);
    zval_ptr_dtor(&name_zv);
    if (!Z_ISUNDEF(retval)) { zval_ptr_dtor(&retval); }

    { std::lock_guard<std::mutex> _mg(obj->lock);
    auto res = obj->counters.emplace(key, zval{});
    if (!res.second) {
        /* another thread inserted first — free our new object */
        zval_ptr_dtor(&new_counter);
        RETURN_ZVAL(&res.first->second, 1, 0);
    }
    ZVAL_COPY_VALUE(&res.first->second, &new_counter);
    RETURN_ZVAL(&res.first->second, 1, 0);
    } // unlock
}

PHP_METHOD(KislayPHPCollector, gauge) {
    char *name = nullptr;
    size_t name_len = 0;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(name, name_len)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_collector_t *obj = php_kislayphp_collector_from_obj(Z_OBJ_P(getThis()));
    std::string key = obj->namespace_str + std::string(name, name_len);

    { std::lock_guard<std::mutex> _mg(obj->lock);
    auto it = obj->gauges.find(key);
    if (it != obj->gauges.end()) {
        RETURN_ZVAL(&it->second, 1, 0);
    }
    } // unlock

    zval new_gauge;
    object_init_ex(&new_gauge, kislayphp_gauge_ce);
    zval name_zv, retval;
    ZVAL_STRING(&name_zv, key.c_str());
    ZVAL_UNDEF(&retval);
    zend_call_method_with_1_params(Z_OBJ(new_gauge), kislayphp_gauge_ce,
                                   nullptr, "__construct", &retval, &name_zv);
    zval_ptr_dtor(&name_zv);
    if (!Z_ISUNDEF(retval)) { zval_ptr_dtor(&retval); }

    { std::lock_guard<std::mutex> _mg(obj->lock);
    auto res = obj->gauges.emplace(key, zval{});
    if (!res.second) {
        zval_ptr_dtor(&new_gauge);
        RETURN_ZVAL(&res.first->second, 1, 0);
    }
    ZVAL_COPY_VALUE(&res.first->second, &new_gauge);
    RETURN_ZVAL(&res.first->second, 1, 0);
    } // unlock
}

PHP_METHOD(KislayPHPCollector, histogram) {
    char *name = nullptr;
    size_t name_len = 0;
    zval *buckets_zv = nullptr;
    ZEND_PARSE_PARAMETERS_START(1, 2)
        Z_PARAM_STRING(name, name_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_ARRAY_EX(buckets_zv, 1, 0)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_collector_t *obj = php_kislayphp_collector_from_obj(Z_OBJ_P(getThis()));
    std::string key = obj->namespace_str + std::string(name, name_len);

    { std::lock_guard<std::mutex> _mg(obj->lock);
    auto it = obj->histograms.find(key);
    if (it != obj->histograms.end()) {
        RETURN_ZVAL(&it->second, 1, 0);
    }
    } // unlock

    zval new_hist;
    object_init_ex(&new_hist, kislayphp_histogram_ce);
    zval name_zv, retval;
    ZVAL_STRING(&name_zv, key.c_str());
    ZVAL_UNDEF(&retval);
    if (buckets_zv != nullptr && Z_TYPE_P(buckets_zv) == IS_ARRAY) {
        zend_call_method_with_2_params(Z_OBJ(new_hist), kislayphp_histogram_ce,
                                       nullptr, "__construct", &retval, &name_zv, buckets_zv);
    } else {
        zend_call_method_with_1_params(Z_OBJ(new_hist), kislayphp_histogram_ce,
                                       nullptr, "__construct", &retval, &name_zv);
    }
    zval_ptr_dtor(&name_zv);
    if (!Z_ISUNDEF(retval)) { zval_ptr_dtor(&retval); }

    { std::lock_guard<std::mutex> _mg(obj->lock);
    auto res = obj->histograms.emplace(key, zval{});
    if (!res.second) {
        zval_ptr_dtor(&new_hist);
        RETURN_ZVAL(&res.first->second, 1, 0);
    }
    ZVAL_COPY_VALUE(&res.first->second, &new_hist);
    RETURN_ZVAL(&res.first->second, 1, 0);
    } // unlock
}

PHP_METHOD(KislayPHPCollector, export) {
    ZEND_PARSE_PARAMETERS_NONE();
    php_kislayphp_collector_t *obj = php_kislayphp_collector_from_obj(Z_OBJ_P(getThis()));
    std::string output;

    std::lock_guard<std::mutex> _mg(obj->lock);

    for (auto &kv : obj->counters) {
        zval retval;
        ZVAL_UNDEF(&retval);
        zend_call_method_with_0_params(Z_OBJ(kv.second), kislayphp_counter_ce,
                                       nullptr, "get", &retval);
        zend_long val = Z_ISUNDEF(retval) ? 0 : zval_get_long(&retval);
        if (!Z_ISUNDEF(retval)) { zval_ptr_dtor(&retval); }
        output += "# TYPE " + kv.first + " counter\n";
        output += kv.first + " " + std::to_string(val) + "\n\n";
    }

    for (auto &kv : obj->gauges) {
        zval retval;
        ZVAL_UNDEF(&retval);
        zend_call_method_with_0_params(Z_OBJ(kv.second), kislayphp_gauge_ce,
                                       nullptr, "get", &retval);
        zend_long val = Z_ISUNDEF(retval) ? 0 : zval_get_long(&retval);
        if (!Z_ISUNDEF(retval)) { zval_ptr_dtor(&retval); }
        output += "# TYPE " + kv.first + " gauge\n";
        output += kv.first + " " + std::to_string(val) + "\n\n";
    }

    for (auto &kv : obj->histograms) {
        zval retval;
        ZVAL_UNDEF(&retval);
        zend_call_method_with_0_params(Z_OBJ(kv.second), kislayphp_histogram_ce,
                                       nullptr, "export", &retval);
        if (!Z_ISUNDEF(retval)) {
            if (Z_TYPE(retval) == IS_STRING) {
                output += std::string(Z_STRVAL(retval), Z_STRLEN(retval));
                output += "\n";
            }
            zval_ptr_dtor(&retval);
        }
    }

    RETURN_STRING(output.c_str());
}

static const zend_function_entry kislayphp_collector_methods[] = {
    PHP_ME(KislayPHPCollector, __construct, arginfo_kislayphp_collector_construct, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPCollector, counter,     arginfo_kislayphp_collector_named,     ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPCollector, gauge,       arginfo_kislayphp_collector_named,     ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPCollector, histogram,   arginfo_kislayphp_collector_histogram, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPCollector, export,      arginfo_kislayphp_metrics_void,        ZEND_ACC_PUBLIC)
    PHP_FE_END
};

/* ----------------------------------------------------------------------- */

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
    ZEND_ABSTRACT_ME(KislayPHPMetricsClientInterface, inc,   arginfo_kislayphp_metrics_inc)
    ZEND_ABSTRACT_ME(KislayPHPMetricsClientInterface, dec,   arginfo_kislayphp_metrics_inc)
    ZEND_ABSTRACT_ME(KislayPHPMetricsClientInterface, get,   arginfo_kislayphp_metrics_get)
    ZEND_ABSTRACT_ME(KislayPHPMetricsClientInterface, all,   arginfo_kislayphp_metrics_void)
    ZEND_ABSTRACT_ME(KislayPHPMetricsClientInterface, reset, arginfo_kislayphp_metrics_reset)
    PHP_FE_END
};

PHP_MINIT_FUNCTION(kislayphp_metrics) {
    zend_class_entry ce;
    INIT_NS_CLASS_ENTRY(ce, "Kislay\\Metrics", "ClientInterface", kislayphp_metrics_client_methods);
    kislayphp_metrics_client_ce = zend_register_internal_interface(&ce);
    zend_register_class_alias("KislayPHP\\Metrics\\ClientInterface", kislayphp_metrics_client_ce);

    INIT_NS_CLASS_ENTRY(ce, "Kislay\\Metrics", "Metrics", kislayphp_metrics_methods);
    kislayphp_metrics_ce = zend_register_internal_class(&ce);
    zend_register_class_alias("KislayPHP\\Metrics\\Metrics", kislayphp_metrics_ce);
    kislayphp_metrics_ce->create_object = kislayphp_metrics_create_object;
    std::memcpy(&kislayphp_metrics_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
    kislayphp_metrics_handlers.offset = XtOffsetOf(php_kislayphp_metrics_t, std);
    kislayphp_metrics_handlers.free_obj = kislayphp_metrics_free_obj;

    /* Counter ------------------------------------------------------------ */
    INIT_NS_CLASS_ENTRY(ce, "Kislay\\Metrics", "Counter", kislayphp_counter_methods);
    kislayphp_counter_ce = zend_register_internal_class(&ce);
    zend_register_class_alias("KislayPHP\\Metrics\\Counter", kislayphp_counter_ce);
    kislayphp_counter_ce->create_object = kislayphp_counter_create_object;
    std::memcpy(&kislayphp_counter_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
    kislayphp_counter_handlers.offset   = XtOffsetOf(php_kislayphp_counter_t, std);
    kislayphp_counter_handlers.free_obj = kislayphp_counter_free_obj;

    /* Gauge -------------------------------------------------------------- */
    INIT_NS_CLASS_ENTRY(ce, "Kislay\\Metrics", "Gauge", kislayphp_gauge_methods);
    kislayphp_gauge_ce = zend_register_internal_class(&ce);
    zend_register_class_alias("KislayPHP\\Metrics\\Gauge", kislayphp_gauge_ce);
    kislayphp_gauge_ce->create_object = kislayphp_gauge_create_object;
    std::memcpy(&kislayphp_gauge_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
    kislayphp_gauge_handlers.offset   = XtOffsetOf(php_kislayphp_gauge_t, std);
    kislayphp_gauge_handlers.free_obj = kislayphp_gauge_free_obj;

    /* Histogram ---------------------------------------------------------- */
    INIT_NS_CLASS_ENTRY(ce, "Kislay\\Metrics", "Histogram", kislayphp_histogram_methods);
    kislayphp_histogram_ce = zend_register_internal_class(&ce);
    zend_register_class_alias("KislayPHP\\Metrics\\Histogram", kislayphp_histogram_ce);
    kislayphp_histogram_ce->create_object = kislayphp_histogram_create_object;
    std::memcpy(&kislayphp_histogram_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
    kislayphp_histogram_handlers.offset   = XtOffsetOf(php_kislayphp_histogram_t, std);
    kislayphp_histogram_handlers.free_obj = kislayphp_histogram_free_obj;

    /* Timer -------------------------------------------------------------- */
    INIT_NS_CLASS_ENTRY(ce, "Kislay\\Metrics", "Timer", kislayphp_timer_methods);
    kislayphp_timer_ce = zend_register_internal_class(&ce);
    zend_register_class_alias("KislayPHP\\Metrics\\Timer", kislayphp_timer_ce);
    kislayphp_timer_ce->create_object = kislayphp_timer_create_object;
    std::memcpy(&kislayphp_timer_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
    kislayphp_timer_handlers.offset   = XtOffsetOf(php_kislayphp_timer_t, std);
    kislayphp_timer_handlers.free_obj = kislayphp_timer_free_obj;

    /* Collector ---------------------------------------------------------- */
    INIT_NS_CLASS_ENTRY(ce, "Kislay\\Metrics", "Collector", kislayphp_collector_methods);
    kislayphp_collector_ce = zend_register_internal_class(&ce);
    zend_register_class_alias("KislayPHP\\Metrics\\Collector", kislayphp_collector_ce);
    kislayphp_collector_ce->create_object = kislayphp_collector_create_object;
    std::memcpy(&kislayphp_collector_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
    kislayphp_collector_handlers.offset   = XtOffsetOf(php_kislayphp_collector_t, std);
    kislayphp_collector_handlers.free_obj = kislayphp_collector_free_obj;

    return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(kislayphp_metrics) {
    return SUCCESS;
}

PHP_RINIT_FUNCTION(kislayphp_metrics) {
    ZVAL_UNDEF(&kislayphp_global_metrics_zval);
    kislayphp_global_metrics_inited = false;
    return SUCCESS;
}

PHP_RSHUTDOWN_FUNCTION(kislayphp_metrics) {
    if (kislayphp_global_metrics_inited) {
        zval_ptr_dtor(&kislayphp_global_metrics_zval);
        ZVAL_UNDEF(&kislayphp_global_metrics_zval);
        kislayphp_global_metrics_inited = false;
    }
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
    PHP_RINIT(kislayphp_metrics),
    PHP_RSHUTDOWN(kislayphp_metrics),
    PHP_MINFO(kislayphp_metrics),
    PHP_KISLAYPHP_METRICS_VERSION,
    STANDARD_MODULE_PROPERTIES
};

#ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE();
#endif

extern "C" {
ZEND_DLEXPORT zend_module_entry *get_module(void) {
    return &kislayphp_metrics_module_entry;
}
}
