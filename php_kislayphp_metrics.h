#ifndef PHP_KISLAYPHP_METRICS_H
#define PHP_KISLAYPHP_METRICS_H

extern "C" {
#include "php.h"
}

#define PHP_KISLAYPHP_METRICS_VERSION "0.1"
#define PHP_KISLAYPHP_METRICS_EXTNAME "kislayphp_metrics"

extern zend_module_entry kislayphp_metrics_module_entry;
#define phpext_kislayphp_metrics_ptr &kislayphp_metrics_module_entry

#endif
