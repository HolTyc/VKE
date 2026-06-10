# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/vakhaib/Desktop/Programy/Grafika/VulkanEngine/build/_deps/stb-src"
  "/home/vakhaib/Desktop/Programy/Grafika/VulkanEngine/build/_deps/stb-build"
  "/home/vakhaib/Desktop/Programy/Grafika/VulkanEngine/build/_deps/stb-subbuild/stb-populate-prefix"
  "/home/vakhaib/Desktop/Programy/Grafika/VulkanEngine/build/_deps/stb-subbuild/stb-populate-prefix/tmp"
  "/home/vakhaib/Desktop/Programy/Grafika/VulkanEngine/build/_deps/stb-subbuild/stb-populate-prefix/src/stb-populate-stamp"
  "/home/vakhaib/Desktop/Programy/Grafika/VulkanEngine/build/_deps/stb-subbuild/stb-populate-prefix/src"
  "/home/vakhaib/Desktop/Programy/Grafika/VulkanEngine/build/_deps/stb-subbuild/stb-populate-prefix/src/stb-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/vakhaib/Desktop/Programy/Grafika/VulkanEngine/build/_deps/stb-subbuild/stb-populate-prefix/src/stb-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/vakhaib/Desktop/Programy/Grafika/VulkanEngine/build/_deps/stb-subbuild/stb-populate-prefix/src/stb-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
