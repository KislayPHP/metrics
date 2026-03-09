extern "C" {
#include "php.h"
#include "ext/standard/info.h"
#include "Zend/zend_API.h"
#include "Zend/zend_interfaces.h"
#include "Zend/zend_exceptions.h"
}

#include "php_kislayphp_metrics.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#ifdef _WIN32
  #include <windows.h>
  #ifndef PTHREAD_WIN32_COMPAT
  #define PTHREAD_WIN32_COMPAT
  typedef CRITICAL_SECTION pthread_mutex_t;
  #define pthread_mutex_init(m, a)   InitializeCriticalSection(m)
  #define pthread_mutex_destroy(m)   DeleteCriticalSection(m)
  #define pthread_mutex_lock(m)      EnterCriticalSection(m)
  #define pthread_mutex_unlock(m)    LeaveCriticalSection(m)
  #endif
#else
  #include <pthread.h>
#endif
#include <cstdlib>
#include <map>
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
        if (error) { *error = "RPC stub unavailable"; }
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
        if (error) { *error = status.error_message(); }
        return false;
    }
    if (!response.ok()) {
        if (error) { *error = response.error(); }
        return false;
    }
    return true;
}

static bool kislayphp_rpc_metrics_get(const std::string &name, zend_long *value, std::string *error) {
    auto *stub = kislayphp_rpc_metrics_stub(kislayphp_rpc_platform_endpoint());
    if (!stub) {
        if (error) { *error = "RPC stub unavailable"; }
        return false;
    }

    kislay::platform::v1::GetMetricRequest request;
    request.set_name(name);
    kislay::platform::v1::GetMetricResponse response;
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(kislayphp_rpc_timeout_ms()));

    grpc::Status status = stub->GetMetric(&context, request, &response);
    if (!status.ok()) {
        if (error) { *error = status.error_message(); }
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
        if (error) { *error = "RPC stub unavailable"; }
        return false;
    }

    kislay::platform::v1::AllMetricsRequest request;
    kislay::platform::v1::AllMetricsResponse response;
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(kislayphp_rpc_timeout_ms()));

    grpc::Status status = stub->AllMetrics(&context, request, &response);
    if (!status.ok()) {
        if (error) { *error = status.error_message(); }
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
        if (error) { *error = "RPC stub unavailable"; }
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
        if (error) { *error = status.error_message(); }
        return false;
    }
    if (!response.ok()) {
        if (error) { *error = response.error(); }
        return false;
    }
    return true;
}
#endif

/* -----------------------------------------------------------------------
 * Label helpers
 * ----------------------------------------------------------------------- */

/* Build a sorted "k=v,k2=v2" string from a PHP array; returns "" if empty. */
static std::string kislayphp_encode_labels(zval *labels_zv) {
    if (labels_zv == nullptr || Z_TYPE_P(labels_zv) != IS_ARRAY
            || zend_hash_num_elements(Z_ARRVAL_P(labels_zv)) == 0) {
        return "";
    }
    std::vector<std::pair<std::string, std::string>> pairs;
    zend_string *key;
    zval *val;
    ZEND_HASH_FOREACH_STR_KEY_VAL(Z_ARRVAL_P(labels_zv), key, val) {
        if (key) {
            zend_string *val_str = zval_get_string(val);
            pairs.emplace_back(ZSTR_VAL(key), ZSTR_VAL(val_str));
            zend_string_release(val_str);
        }
    } ZEND_HASH_FOREACH_END();
    std::sort(pairs.begin(), pairs.end());
    std::string result;
    for (size_t i = 0; i < pairs.size(); ++i) {
        if (i > 0) result += ",";
        result += pairs[i].first + "=" + pairs[i].second;
    }
    return result;
}

/* Combine metric name + encoded labels into a storage key. */
static std::string kislayphp_make_key(const std::string &name, zval *labels_zv) {
    std::string encoded = kislayphp_encode_labels(labels_zv);
    if (encoded.empty()) return name;
    return name + "{" + encoded + "}";
}

/* Return the base metric name (strip labels suffix). */
static std::string kislayphp_base_name(const std::string &key) {
    size_t brace = key.find('{');
    return (brace != std::string::npos) ? key.substr(0, brace) : key;
}

/* Convert internal "k=v,k2=v2" labels to Prometheus-quoted "k=\"v\",k2=\"v2\"". */
static std::string kislayphp_labels_to_prometheus(const std::string &labels_str) {
    std::string result;
    size_t pos = 0;
    bool first = true;
    while (pos < labels_str.size()) {
        size_t eq = labels_str.find('=', pos);
        if (eq == std::string::npos) break;
        std::string k = labels_str.substr(pos, eq - pos);
        size_t comma = labels_str.find(',', eq + 1);
        std::string v;
        if (comma == std::string::npos) {
            v = labels_str.substr(eq + 1);
            pos = labels_str.size();
        } else {
            v = labels_str.substr(eq + 1, comma - eq - 1);
            pos = comma + 1;
        }
        if (!first) result += ",";
        result += k + "=\"" + v + "\"";
        first = false;
    }
    return result;
}

/* Convert an internal storage key to a Prometheus metric line prefix.
 * e.g. "foo{a=1,b=2}" -> "foo{a=\"1\",b=\"2\"}" */
static std::string kislayphp_key_to_prom(const std::string &key) {
    size_t brace = key.find('{');
    if (brace == std::string::npos) return key;
    std::string base = key.substr(0, brace);
    std::string labels_str = key.substr(brace + 1, key.size() - brace - 2);
    return base + "{" + kislayphp_labels_to_prometheus(labels_str) + "}";
}

/* Build a histogram bucket line like: name_bucket{[existing_labels,]le="0.005"} N */
static std::string kislayphp_histogram_bucket_line(const std::string &internal_key,
                                                    const std::string &le,
                                                    zend_long count) {
    size_t brace = internal_key.find('{');
    char buf[256];
    if (brace == std::string::npos) {
        snprintf(buf, sizeof(buf), "%s_bucket{le=\"%s\"} %ld\n",
                 internal_key.c_str(), le.c_str(), (long)count);
    } else {
        std::string base = internal_key.substr(0, brace);
        std::string labels_str = internal_key.substr(brace + 1, internal_key.size() - brace - 2);
        std::string prom_labels = kislayphp_labels_to_prometheus(labels_str);
        snprintf(buf, sizeof(buf), "%s_bucket{%s,le=\"%s\"} %ld\n",
                 base.c_str(), prom_labels.c_str(), le.c_str(), (long)count);
    }
    return std::string(buf);
}

/* -----------------------------------------------------------------------
 * Object struct
 * ----------------------------------------------------------------------- */

typedef struct _php_kislayphp_metrics_t {
    std::unordered_map<std::string, zend_long>  counters;
    std::unordered_map<std::string, double>     gauges;
    std::unordered_map<std::string, std::vector<double>> histogram_observations;
    std::unordered_map<int, std::pair<std::string, std::chrono::steady_clock::time_point>> active_timers;
    std::atomic<int> next_timer_id;
    pthread_mutex_t  lock;
    zval             client;
    bool             has_client;
    zend_object      std;
} php_kislayphp_metrics_t;

static zend_object_handlers kislayphp_metrics_handlers;

static inline php_kislayphp_metrics_t *php_kislayphp_metrics_from_obj(zend_object *obj) {
    return reinterpret_cast<php_kislayphp_metrics_t *>(
        reinterpret_cast<char *>(obj) - XtOffsetOf(php_kislayphp_metrics_t, std));
}

static const std::vector<double> kislayphp_default_histogram_buckets = {
    0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0, 2.5, 5.0, 10.0
};

static zend_object *kislayphp_metrics_create_object(zend_class_entry *ce) {
    php_kislayphp_metrics_t *obj = static_cast<php_kislayphp_metrics_t *>(
        ecalloc(1, sizeof(php_kislayphp_metrics_t) + zend_object_properties_size(ce)));
    zend_object_std_init(&obj->std, ce);
    object_properties_init(&obj->std, ce);
    new (&obj->counters)              std::unordered_map<std::string, zend_long>();
    new (&obj->gauges)                std::unordered_map<std::string, double>();
    new (&obj->histogram_observations)std::unordered_map<std::string, std::vector<double>>();
    new (&obj->active_timers)         std::unordered_map<int, std::pair<std::string, std::chrono::steady_clock::time_point>>();
    new (&obj->next_timer_id)         std::atomic<int>(0);
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
    obj->gauges.~unordered_map();
    obj->histogram_observations.~unordered_map();
    obj->active_timers.~unordered_map();
    obj->next_timer_id.~atomic();
    pthread_mutex_destroy(&obj->lock);
    zend_object_std_dtor(&obj->std);
}

/* -----------------------------------------------------------------------
 * Argument info
 * ----------------------------------------------------------------------- */

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_metrics_void, 0, 0, 0)
ZEND_END_ARG_INFO()

/* inc(name, by=1, labels=[]) */
ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_metrics_inc, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, by, IS_LONG, 1)
    ZEND_ARG_TYPE_INFO(0, labels, IS_ARRAY, 1)
ZEND_END_ARG_INFO()

/* get(name) */
ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_metrics_get, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 0)
ZEND_END_ARG_INFO()

/* get(name, labels=[]) — used for getGauge */
ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_metrics_get_labels, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, labels, IS_ARRAY, 1)
ZEND_END_ARG_INFO()

/* setClient(client) */
ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_metrics_set_client, 0, 0, 1)
    ZEND_ARG_OBJ_INFO(0, client, Kislay\\Metrics\\ClientInterface, 0)
ZEND_END_ARG_INFO()

/* reset(?name) */
ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_metrics_reset, 0, 0, 0)
    ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 1)
ZEND_END_ARG_INFO()

/* gauge(name, value, labels=[]) */
ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_metrics_gauge, 0, 0, 2)
    ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, value, IS_DOUBLE, 0)
    ZEND_ARG_TYPE_INFO(0, labels, IS_ARRAY, 1)
ZEND_END_ARG_INFO()

/* gaugeInc/gaugeDec(name, by=1.0, labels=[]) */
ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_metrics_gauge_delta, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, by, IS_DOUBLE, 1)
    ZEND_ARG_TYPE_INFO(0, labels, IS_ARRAY, 1)
ZEND_END_ARG_INFO()

/* observe(name, value, labels=[]) */
ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_metrics_observe, 0, 0, 2)
    ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, value, IS_DOUBLE, 0)
    ZEND_ARG_TYPE_INFO(0, labels, IS_ARRAY, 1)
ZEND_END_ARG_INFO()

/* startTimer(name, labels=[]) */
ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_metrics_start_timer, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, labels, IS_ARRAY, 1)
ZEND_END_ARG_INFO()

/* stopTimer(timerId) */
ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_metrics_stop_timer, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, timerId, IS_LONG, 0)
ZEND_END_ARG_INFO()

/* -----------------------------------------------------------------------
 * Method implementations
 * ----------------------------------------------------------------------- */

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

/* inc(name, by=1, labels=[]) */
PHP_METHOD(KislayPHPMetrics, inc) {
    char *name = nullptr;
    size_t name_len = 0;
    zend_long by = 1;
    zval *labels_zv = nullptr;
    ZEND_PARSE_PARAMETERS_START(1, 3)
        Z_PARAM_STRING(name, name_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_LONG(by)
        Z_PARAM_ARRAY_EX(labels_zv, 1, 0)
    ZEND_PARSE_PARAMETERS_END();

    std::string key = kislayphp_make_key(std::string(name, name_len), labels_zv);

    php_kislayphp_metrics_t *obj = php_kislayphp_metrics_from_obj(Z_OBJ_P(getThis()));
    if (obj->has_client) {
        zval name_zv, by_zv;
        ZVAL_STRING(&name_zv, key.c_str());
        ZVAL_LONG(&by_zv, by);
        zval retval;
        ZVAL_UNDEF(&retval);
        zend_call_method_with_2_params(Z_OBJ(obj->client), Z_OBJCE(obj->client), nullptr, "inc", &retval, &name_zv, &by_zv);
        zval_ptr_dtor(&name_zv);
        zval_ptr_dtor(&by_zv);
        if (Z_ISUNDEF(retval)) { RETURN_TRUE; }
        RETVAL_ZVAL(&retval, 1, 1);
        return;
    }

#ifdef KISLAYPHP_RPC
    if (kislayphp_rpc_enabled()) {
        std::string error;
        if (kislayphp_rpc_metrics_inc(key, by, &error)) {
            RETURN_TRUE;
        }
    }
#endif

    pthread_mutex_lock(&obj->lock);
    obj->counters[key] += by;
    pthread_mutex_unlock(&obj->lock);
    RETURN_TRUE;
}

/* dec(name, by=1, labels=[]) */
PHP_METHOD(KislayPHPMetrics, dec) {
    char *name = nullptr;
    size_t name_len = 0;
    zend_long by = 1;
    zval *labels_zv = nullptr;
    ZEND_PARSE_PARAMETERS_START(1, 3)
        Z_PARAM_STRING(name, name_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_LONG(by)
        Z_PARAM_ARRAY_EX(labels_zv, 1, 0)
    ZEND_PARSE_PARAMETERS_END();

    if (by < 0) { by = -by; }

    std::string key = kislayphp_make_key(std::string(name, name_len), labels_zv);

    php_kislayphp_metrics_t *obj = php_kislayphp_metrics_from_obj(Z_OBJ_P(getThis()));
    if (obj->has_client) {
        zval name_zv, by_zv;
        ZVAL_STRING(&name_zv, key.c_str());
        ZVAL_LONG(&by_zv, -by);
        zval retval;
        ZVAL_UNDEF(&retval);
        zend_call_method_with_2_params(Z_OBJ(obj->client), Z_OBJCE(obj->client), nullptr, "inc", &retval, &name_zv, &by_zv);
        zval_ptr_dtor(&name_zv);
        zval_ptr_dtor(&by_zv);
        if (Z_ISUNDEF(retval)) { RETURN_TRUE; }
        RETVAL_ZVAL(&retval, 1, 1);
        return;
    }

#ifdef KISLAYPHP_RPC
    if (kislayphp_rpc_enabled()) {
        std::string error;
        if (kislayphp_rpc_metrics_inc(key, -by, &error)) {
            RETURN_TRUE;
        }
    }
#endif

    pthread_mutex_lock(&obj->lock);
    obj->counters[key] -= by;
    pthread_mutex_unlock(&obj->lock);
    RETURN_TRUE;
}

/* get(name): int */
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
        if (Z_ISUNDEF(retval)) { RETURN_LONG(0); }
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
    pthread_mutex_lock(&obj->lock);
    auto it = obj->counters.find(std::string(name, name_len));
    if (it != obj->counters.end()) { value = it->second; found = true; }
    pthread_mutex_unlock(&obj->lock);
    if (!found) { RETURN_LONG(0); }
    RETURN_LONG(value);
}

/* all(): array */
PHP_METHOD(KislayPHPMetrics, all) {
    php_kislayphp_metrics_t *obj = php_kislayphp_metrics_from_obj(Z_OBJ_P(getThis()));
    if (obj->has_client) {
        zval retval;
        ZVAL_UNDEF(&retval);
        zend_call_method_with_0_params(Z_OBJ(obj->client), Z_OBJCE(obj->client), nullptr, "all", &retval);
        if (Z_ISUNDEF(retval)) { array_init(return_value); return; }
        RETVAL_ZVAL(&retval, 1, 1);
        return;
    }

#ifdef KISLAYPHP_RPC
    if (kislayphp_rpc_enabled()) {
        std::string error;
        if (kislayphp_rpc_metrics_all(return_value, &error)) { return; }
    }
#endif

    array_init(return_value);
    pthread_mutex_lock(&obj->lock);
    for (const auto &entry : obj->counters) {
        add_assoc_long(return_value, entry.first.c_str(), entry.second);
    }
    pthread_mutex_unlock(&obj->lock);
}

/* reset(?name) */
PHP_METHOD(KislayPHPMetrics, reset) {
    char *name = nullptr;
    size_t name_len = 0;
    bool has_name = false;
    ZEND_PARSE_PARAMETERS_START(0, 1)
        Z_PARAM_OPTIONAL
        Z_PARAM_STRING(name, name_len)
    ZEND_PARSE_PARAMETERS_END();
    if (ZEND_NUM_ARGS() == 1) { has_name = true; }

    php_kislayphp_metrics_t *obj = php_kislayphp_metrics_from_obj(Z_OBJ_P(getThis()));
    if (obj->has_client && has_name) {
        zval name_zv, zero_zv;
        ZVAL_STRINGL(&name_zv, name, name_len);
        ZVAL_LONG(&zero_zv, 0);
        zval retval;
        ZVAL_UNDEF(&retval);
        zend_call_method_with_2_params(Z_OBJ(obj->client), Z_OBJCE(obj->client), nullptr, "inc", &retval, &name_zv, &zero_zv);
        zval_ptr_dtor(&name_zv);
        zval_ptr_dtor(&zero_zv);
        if (!Z_ISUNDEF(retval)) { zval_ptr_dtor(&retval); }
        RETURN_TRUE;
    }

#ifdef KISLAYPHP_RPC
    if (kislayphp_rpc_enabled()) {
        std::string error;
        std::string name_value = has_name ? std::string(name, name_len) : std::string();
        if (kislayphp_rpc_metrics_reset(name_value, !has_name, &error)) { RETURN_TRUE; }
    }
#endif

    pthread_mutex_lock(&obj->lock);
    if (has_name) {
        obj->counters[std::string(name, name_len)] = 0;
    } else {
        obj->counters.clear();
    }
    pthread_mutex_unlock(&obj->lock);
    RETURN_TRUE;
}

/* -----------------------------------------------------------------------
 * Gauge methods
 * ----------------------------------------------------------------------- */

/* gauge(name, value, labels=[]): void — set gauge to exact value */
PHP_METHOD(KislayPHPMetrics, gauge) {
    char *name = nullptr;
    size_t name_len = 0;
    double value = 0.0;
    zval *labels_zv = nullptr;
    ZEND_PARSE_PARAMETERS_START(2, 3)
        Z_PARAM_STRING(name, name_len)
        Z_PARAM_DOUBLE(value)
        Z_PARAM_OPTIONAL
        Z_PARAM_ARRAY_EX(labels_zv, 1, 0)
    ZEND_PARSE_PARAMETERS_END();

    std::string key = kislayphp_make_key(std::string(name, name_len), labels_zv);
    php_kislayphp_metrics_t *obj = php_kislayphp_metrics_from_obj(Z_OBJ_P(getThis()));

    pthread_mutex_lock(&obj->lock);
    obj->gauges[key] = value;
    pthread_mutex_unlock(&obj->lock);
}

/* gaugeInc(name, by=1.0, labels=[]): void — increment gauge */
PHP_METHOD(KislayPHPMetrics, gaugeInc) {
    char *name = nullptr;
    size_t name_len = 0;
    double by = 1.0;
    zval *labels_zv = nullptr;
    ZEND_PARSE_PARAMETERS_START(1, 3)
        Z_PARAM_STRING(name, name_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_DOUBLE(by)
        Z_PARAM_ARRAY_EX(labels_zv, 1, 0)
    ZEND_PARSE_PARAMETERS_END();

    std::string key = kislayphp_make_key(std::string(name, name_len), labels_zv);
    php_kislayphp_metrics_t *obj = php_kislayphp_metrics_from_obj(Z_OBJ_P(getThis()));

    pthread_mutex_lock(&obj->lock);
    obj->gauges[key] += by;
    pthread_mutex_unlock(&obj->lock);
}

/* gaugeDec(name, by=1.0, labels=[]): void — decrement gauge */
PHP_METHOD(KislayPHPMetrics, gaugeDec) {
    char *name = nullptr;
    size_t name_len = 0;
    double by = 1.0;
    zval *labels_zv = nullptr;
    ZEND_PARSE_PARAMETERS_START(1, 3)
        Z_PARAM_STRING(name, name_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_DOUBLE(by)
        Z_PARAM_ARRAY_EX(labels_zv, 1, 0)
    ZEND_PARSE_PARAMETERS_END();

    std::string key = kislayphp_make_key(std::string(name, name_len), labels_zv);
    php_kislayphp_metrics_t *obj = php_kislayphp_metrics_from_obj(Z_OBJ_P(getThis()));

    pthread_mutex_lock(&obj->lock);
    obj->gauges[key] -= by;
    pthread_mutex_unlock(&obj->lock);
}

/* getGauge(name, labels=[]): float */
PHP_METHOD(KislayPHPMetrics, getGauge) {
    char *name = nullptr;
    size_t name_len = 0;
    zval *labels_zv = nullptr;
    ZEND_PARSE_PARAMETERS_START(1, 2)
        Z_PARAM_STRING(name, name_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_ARRAY_EX(labels_zv, 1, 0)
    ZEND_PARSE_PARAMETERS_END();

    std::string key = kislayphp_make_key(std::string(name, name_len), labels_zv);
    php_kislayphp_metrics_t *obj = php_kislayphp_metrics_from_obj(Z_OBJ_P(getThis()));

    double value = 0.0;
    pthread_mutex_lock(&obj->lock);
    auto it = obj->gauges.find(key);
    if (it != obj->gauges.end()) { value = it->second; }
    pthread_mutex_unlock(&obj->lock);

    RETURN_DOUBLE(value);
}

/* -----------------------------------------------------------------------
 * Histogram / timer methods
 * ----------------------------------------------------------------------- */

/* observe(name, value, labels=[]): void — record a value */
PHP_METHOD(KislayPHPMetrics, observe) {
    char *name = nullptr;
    size_t name_len = 0;
    double value = 0.0;
    zval *labels_zv = nullptr;
    ZEND_PARSE_PARAMETERS_START(2, 3)
        Z_PARAM_STRING(name, name_len)
        Z_PARAM_DOUBLE(value)
        Z_PARAM_OPTIONAL
        Z_PARAM_ARRAY_EX(labels_zv, 1, 0)
    ZEND_PARSE_PARAMETERS_END();

    std::string key = kislayphp_make_key(std::string(name, name_len), labels_zv);
    php_kislayphp_metrics_t *obj = php_kislayphp_metrics_from_obj(Z_OBJ_P(getThis()));

    pthread_mutex_lock(&obj->lock);
    obj->histogram_observations[key].push_back(value);
    pthread_mutex_unlock(&obj->lock);
}

/* startTimer(name, labels=[]): int — returns timer_id */
PHP_METHOD(KislayPHPMetrics, startTimer) {
    char *name = nullptr;
    size_t name_len = 0;
    zval *labels_zv = nullptr;
    ZEND_PARSE_PARAMETERS_START(1, 2)
        Z_PARAM_STRING(name, name_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_ARRAY_EX(labels_zv, 1, 0)
    ZEND_PARSE_PARAMETERS_END();

    std::string key = kislayphp_make_key(std::string(name, name_len), labels_zv);
    php_kislayphp_metrics_t *obj = php_kislayphp_metrics_from_obj(Z_OBJ_P(getThis()));

    int timer_id = ++obj->next_timer_id;
    auto now = std::chrono::steady_clock::now();

    pthread_mutex_lock(&obj->lock);
    obj->active_timers[timer_id] = {key, now};
    pthread_mutex_unlock(&obj->lock);

    RETURN_LONG(static_cast<zend_long>(timer_id));
}

/* stopTimer(timerId): float — returns elapsed ms, records to histogram */
PHP_METHOD(KislayPHPMetrics, stopTimer) {
    zend_long timer_id;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_LONG(timer_id)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_metrics_t *obj = php_kislayphp_metrics_from_obj(Z_OBJ_P(getThis()));

    pthread_mutex_lock(&obj->lock);
    auto it = obj->active_timers.find(static_cast<int>(timer_id));
    if (it == obj->active_timers.end()) {
        pthread_mutex_unlock(&obj->lock);
        RETURN_DOUBLE(-1.0);
    }

    std::string metric_key = it->second.first;
    auto start_time = it->second.second;
    obj->active_timers.erase(it);

    auto now = std::chrono::steady_clock::now();
    double elapsed_ms = std::chrono::duration<double, std::milli>(now - start_time).count();

    obj->histogram_observations[metric_key].push_back(elapsed_ms);
    pthread_mutex_unlock(&obj->lock);

    RETURN_DOUBLE(elapsed_ms);
}

/* -----------------------------------------------------------------------
 * Export methods
 * ----------------------------------------------------------------------- */

/* exportPrometheus(): string — Prometheus text format 0.0.4 */
PHP_METHOD(KislayPHPMetrics, exportPrometheus) {
    ZEND_PARSE_PARAMETERS_NONE();

    php_kislayphp_metrics_t *obj = php_kislayphp_metrics_from_obj(Z_OBJ_P(getThis()));
    std::string output;

    pthread_mutex_lock(&obj->lock);

    /* --- Counters -------------------------------------------------------- */
    {
        std::map<std::string, std::vector<std::pair<std::string, zend_long>>> groups;
        for (const auto &e : obj->counters) {
            groups[kislayphp_base_name(e.first)].emplace_back(e.first, e.second);
        }
        for (const auto &g : groups) {
            const std::string &base = g.first;
            output += "# HELP " + base + " Counter: " + base + "\n";
            output += "# TYPE " + base + " counter\n";
            for (const auto &e : g.second) {
                output += kislayphp_key_to_prom(e.first) + " " + std::to_string(e.second) + "\n";
            }
            output += "\n";
        }
    }

    /* --- Gauges ---------------------------------------------------------- */
    {
        std::map<std::string, std::vector<std::pair<std::string, double>>> groups;
        for (const auto &e : obj->gauges) {
            groups[kislayphp_base_name(e.first)].emplace_back(e.first, e.second);
        }
        for (const auto &g : groups) {
            const std::string &base = g.first;
            output += "# HELP " + base + " Gauge: " + base + "\n";
            output += "# TYPE " + base + " gauge\n";
            for (const auto &e : g.second) {
                char buf[64];
                snprintf(buf, sizeof(buf), "%f", e.second);
                output += kislayphp_key_to_prom(e.first) + " " + buf + "\n";
            }
            output += "\n";
        }
    }

    /* --- Histograms ------------------------------------------------------ */
    {
        std::map<std::string, std::vector<std::pair<std::string, std::vector<double>>>> groups;
        for (const auto &e : obj->histogram_observations) {
            groups[kislayphp_base_name(e.first)].emplace_back(e.first, e.second);
        }
        for (const auto &g : groups) {
            const std::string &base = g.first;
            output += "# HELP " + base + " Histogram: " + base + "\n";
            output += "# TYPE " + base + " histogram\n";

            for (const auto &e : g.second) {
                const std::string &key = e.first;
                const std::vector<double> &obs = e.second;

                /* Compute sum and count */
                double sum = 0.0;
                for (double v : obs) { sum += v; }
                zend_long count = static_cast<zend_long>(obs.size());

                /* Bucket counts */
                for (double bucket : kislayphp_default_histogram_buckets) {
                    zend_long bucket_count = 0;
                    for (double v : obs) {
                        if (v <= bucket) { ++bucket_count; }
                    }
                    char le_buf[32];
                    snprintf(le_buf, sizeof(le_buf), "%g", bucket);
                    output += kislayphp_histogram_bucket_line(key, le_buf, bucket_count);
                }
                output += kislayphp_histogram_bucket_line(key, "+Inf", count);

                char sum_buf[64];
                snprintf(sum_buf, sizeof(sum_buf), "%f", sum);
                std::string prom_key = kislayphp_key_to_prom(key);
                output += kislayphp_base_name(key) + "_sum";
                {
                    size_t brace = prom_key.find('{');
                    if (brace != std::string::npos) {
                        output += prom_key.substr(brace);
                    }
                }
                output += " " + std::string(sum_buf) + "\n";

                output += kislayphp_base_name(key) + "_count";
                {
                    size_t brace = prom_key.find('{');
                    if (brace != std::string::npos) {
                        output += prom_key.substr(brace);
                    }
                }
                output += " " + std::to_string(count) + "\n";
            }
            output += "\n";
        }
    }

    pthread_mutex_unlock(&obj->lock);

    RETURN_STRING(output.c_str());
}

/* exportJSON(): array — all metrics as nested array for Actuator /metrics */
PHP_METHOD(KislayPHPMetrics, exportJSON) {
    ZEND_PARSE_PARAMETERS_NONE();

    php_kislayphp_metrics_t *obj = php_kislayphp_metrics_from_obj(Z_OBJ_P(getThis()));

    array_init(return_value);

    zval counters_arr, gauges_arr, histograms_arr;
    array_init(&counters_arr);
    array_init(&gauges_arr);
    array_init(&histograms_arr);

    pthread_mutex_lock(&obj->lock);

    for (const auto &e : obj->counters) {
        add_assoc_long(&counters_arr, e.first.c_str(), e.second);
    }

    for (const auto &e : obj->gauges) {
        add_assoc_double(&gauges_arr, e.first.c_str(), e.second);
    }

    for (const auto &e : obj->histogram_observations) {
        zval hist_entry;
        array_init(&hist_entry);

        zval obs_arr;
        array_init(&obs_arr);
        double sum = 0.0;
        for (double v : e.second) {
            add_next_index_double(&obs_arr, v);
            sum += v;
        }

        add_assoc_zval(&hist_entry, "observations", &obs_arr);
        add_assoc_long(&hist_entry, "count", static_cast<zend_long>(e.second.size()));
        add_assoc_double(&hist_entry, "sum", sum);

        add_assoc_zval(&histograms_arr, e.first.c_str(), &hist_entry);
    }

    pthread_mutex_unlock(&obj->lock);

    add_assoc_zval(return_value, "counters", &counters_arr);
    add_assoc_zval(return_value, "gauges", &gauges_arr);
    add_assoc_zval(return_value, "histograms", &histograms_arr);
}

/* -----------------------------------------------------------------------
 * Method / interface tables
 * ----------------------------------------------------------------------- */

static const zend_function_entry kislayphp_metrics_methods[] = {
    PHP_ME(KislayPHPMetrics, __construct,     arginfo_kislayphp_metrics_void,        ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPMetrics, setClient,       arginfo_kislayphp_metrics_set_client,  ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPMetrics, inc,             arginfo_kislayphp_metrics_inc,         ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPMetrics, dec,             arginfo_kislayphp_metrics_inc,         ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPMetrics, get,             arginfo_kislayphp_metrics_get,         ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPMetrics, all,             arginfo_kislayphp_metrics_void,        ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPMetrics, reset,           arginfo_kislayphp_metrics_reset,       ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPMetrics, gauge,           arginfo_kislayphp_metrics_gauge,       ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPMetrics, gaugeInc,        arginfo_kislayphp_metrics_gauge_delta, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPMetrics, gaugeDec,        arginfo_kislayphp_metrics_gauge_delta, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPMetrics, getGauge,        arginfo_kislayphp_metrics_get_labels,  ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPMetrics, observe,         arginfo_kislayphp_metrics_observe,     ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPMetrics, startTimer,      arginfo_kislayphp_metrics_start_timer, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPMetrics, stopTimer,       arginfo_kislayphp_metrics_stop_timer,  ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPMetrics, exportPrometheus,arginfo_kislayphp_metrics_void,        ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPMetrics, exportJSON,      arginfo_kislayphp_metrics_void,        ZEND_ACC_PUBLIC)
    PHP_FE_END
};

static const zend_function_entry kislayphp_metrics_client_methods[] = {
    ZEND_ABSTRACT_ME(KislayPHPMetricsClientInterface, inc, arginfo_kislayphp_metrics_inc)
    ZEND_ABSTRACT_ME(KislayPHPMetricsClientInterface, get, arginfo_kislayphp_metrics_get)
    ZEND_ABSTRACT_ME(KislayPHPMetricsClientInterface, all, arginfo_kislayphp_metrics_void)
    PHP_FE_END
};

/* -----------------------------------------------------------------------
 * Module lifecycle
 * ----------------------------------------------------------------------- */

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

#ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE();
#endif

extern "C" {
ZEND_DLEXPORT zend_module_entry *get_module(void) {
    return &kislayphp_metrics_module_entry;
}
}
