cmake_minimum_required(VERSION 3.29)
project(CoRigNativeGain LANGUAGES CXX)
find_package(CoRigSdk 2 REQUIRED CONFIG)
corig_add_plugin(NativeGain MANIFEST plugin.json SOURCES gain.cpp)
