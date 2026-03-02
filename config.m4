PHP_ARG_ENABLE(kislayphp_metrics, whether to enable kislayphp_metrics,
[  --enable-kislayphp_metrics   Enable kislayphp_metrics support])

if test "$PHP_KISLAYPHP_METRICS" != "no"; then
  PHP_REQUIRE_CXX()
  PHP_ADD_LIBRARY(stdc++,, KISLAYPHP_METRICS_SHARED_LIBADD)
  if test -f ../rpc/gen/platform.pb.cc; then
    RPC_GEN_DIR=`pwd`/../rpc/gen
    PHP_ADD_INCLUDE($RPC_GEN_DIR)
    PHP_ADD_INCLUDE(`pwd`/../rpc)
    PKG_CHECK_MODULES([GRPC], [grpc++])
    PHP_EVAL_INCLINE($GRPC_CFLAGS)
    PHP_EVAL_LIBLINE($GRPC_LIBS, KISLAYPHP_METRICS_SHARED_LIBADD)
    CXXFLAGS="$CXXFLAGS -DKISLAYPHP_RPC"
    RPC_SRCS="../rpc/gen/platform.pb.cc ../rpc/gen/platform.grpc.pb.cc"
  else
    AC_MSG_WARN([RPC stubs not found. Building without RPC support])
    RPC_SRCS=""
  fi

  PHP_NEW_EXTENSION(kislayphp_metrics, kislayphp_metrics.cpp $RPC_SRCS, $ext_shared)
  PHP_SUBST(KISLAYPHP_METRICS_SHARED_LIBADD)
fi
