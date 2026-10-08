add_repositories("qitrader-repo git@github.com:qitrader/repo.git")

add_requires("fmt", "openssl", "cryptopp", "glog", "liburing", "jsoncpp", "httpcpp")
-- httpcpp 的预编译静态库依赖 boost 1.89 的 ABI，锁定版本保证构建可复现；
-- 该版本的 cmake 构建脚本在较新的 cmake 上会失败，这里回退到 b2 构建。
add_requires("boost[hash2,asio,beast,url,json,system,program_options,multiprecision,pfr,math,chrono,filesystem,serialization,thread] 1.89.0", {configs = {cmake = false}})
add_rules("plugin.compile_commands.autoupdate", {outputdir = "build/"})
set_languages("c++23")

-- 默认调试模式；发布构建请用 xmake config --mode=release
add_rules("mode.debug")

target("qitrader")
    set_kind("binary")
    add_includedirs(".", "market/", "common/", "engine/", "strategy/", "notice/", "backtest/", "core/")

    add_files("market/**/*.cpp")
    add_files("common/**/*.cpp")
    add_files("notice/**/*.cpp")
    add_files("*.cpp")
    add_files("engine/*.cpp")
    add_files("strategy/**/*.cpp")
    add_files("backtest/**/*.cpp")
    add_files("core/**/*.cpp")

    add_packages("httpcpp", "fmt", "openssl", "glog","cryptopp", "liburing", "jsoncpp")
    add_packages("boost")
    -- httpcpp 静态库依赖 boost::urls，必须显式补上该静态库，
    -- 否则链接期会出现 boost::urls::parse_uri 等符号未定义。
    add_links("boost_url")
    add_defines("BOOST_ASIO_HAS_IO_URING", "BOOST_ASIO_HAS_FILE")

target("qitrader-core-tests")
    set_kind("binary")
    add_includedirs(".", "common/", "core/", "strategy/")
    add_files("tests/core_runtime_test.cpp")
    add_files("strategy/multilevel/actor_critic.cpp")
    add_files("core/portfolio/portfolio_ledger.cpp")
    add_files("core/risk/risk_manager.cpp")
    add_files("core/execution/order_manager.cpp")
    add_files("core/environment/market_making_environment.cpp")
    add_packages("boost", "fmt", "jsoncpp")
    -- 断言用 assert 实现：release 模式下 NDEBUG 会把断言全部编译掉，
    -- 测试会"恒绿"。这里强制取消 NDEBUG，保证任何模式下断言都生效。
    add_cxflags("-UNDEBUG")
