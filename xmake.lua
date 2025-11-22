add_repositories("qitrader-repo git@github.com:qitrader/repo.git")

add_requires("fmt", "openssl", "cryptopp", "glog", "liburing", "jsoncpp", "httpcpp")
add_requires("boost[hash2,asio,beast,url,json,system,program_options,multiprecision,pfr,math,chrono,filesystem,serialization,thread]")
add_rules("plugin.compile_commands.autoupdate", {outputdir = "build/"})
set_languages("c++23")

add_rules("mode.debug")

target("qitrader")
    set_kind("binary")
    add_includedirs("market/", "common/", "engine/", "strategy/", "notice/")

    add_files("market/**/*.cpp")
    add_files("common/**/*.cpp")
    add_files("notice/**/*.cpp")
    add_files("*.cpp")
    add_files("engine/*.cpp")
    add_files("strategy/**/*.cpp")
    
    add_packages("httpcpp", "fmt", "openssl", "glog","cryptopp", "liburing", "jsoncpp")
    add_packages("boost")
    add_defines("BOOST_ASIO_HAS_IO_URING", "BOOST_ASIO_HAS_FILE")
    set_toolset("cxx", "clang")
    set_toolset("ld", "clang++")
