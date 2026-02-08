PHP_ARG_ENABLE(kislayphp_metrics, whether to enable kislayphp_metrics,
[  --enable-kislayphp_metrics   Enable kislayphp_metrics support])

if test "$PHP_KISLAYPHP_METRICS" != "no"; then
  PHP_REQUIRE_CXX()
  PHP_NEW_EXTENSION(kislayphp_metrics, kislayphp_metrics.cpp, $ext_shared)
fi
