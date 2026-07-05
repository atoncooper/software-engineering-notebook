# tests/CMakeLists.txt
# 暂无独立集成测试,模块测试通过 408-cli test 触发 (CS408_REGISTER_MODULE 的 test 函数)
# 如需独立 test 可执行,可在此添加:
#
# add_executable(integration_test integration_test.cpp)
# target_link_libraries(integration_test PRIVATE 408_modules)
# add_test(NAME integration COMMAND integration_test)
