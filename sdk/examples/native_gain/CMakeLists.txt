cmake_minimum_required(VERSION 3.25)
project(CoRigNativeGain LANGUAGES CXX)
find_package(CoRigSdk 1 REQUIRED CONFIG)
corig_add_plugin(NativeGain MANIFEST plugin.json SOURCES gain.cpp)
