# Copyright 2009-2012, Iowa State University
# Copyright 2011-2015, Contributors
# Distributed under the Boost Software License, Version 1.0
# See copy at http://www.boost.org/LICENSE_1_0.txt
# SPDX-License-Identifier: BSL-1.0

#
# Internal file for GetGitDescription.cmake
#
# Original author:
# 2009-2010 Ryan Pavlik <rpavlik@iastate.edu> <abiryan@ryand.net>
#

set(HEAD_HASH)

file(READ "/home/jiping/FENGSim/toolkit/CEM/palace-0.13.0/palace/build/CMakeFiles/git-data/HEAD" HEAD_CONTENTS LIMIT 1024)

string(STRIP "${HEAD_CONTENTS}" HEAD_CONTENTS)
if(HEAD_CONTENTS MATCHES "ref")  # named branch
  string(REPLACE "ref: " "" HEAD_REF "${HEAD_CONTENTS}")
  if(EXISTS "/home/jiping/FENGSim/toolkit/CEM/.git/${HEAD_REF}")
    configure_file("/home/jiping/FENGSim/toolkit/CEM/.git/${HEAD_REF}" "/home/jiping/FENGSim/toolkit/CEM/palace-0.13.0/palace/build/CMakeFiles/git-data/head-ref" COPYONLY)
  else()
    configure_file("/home/jiping/FENGSim/toolkit/CEM/.git/packed-refs" "/home/jiping/FENGSim/toolkit/CEM/palace-0.13.0/palace/build/CMakeFiles/git-data/packed-refs" COPYONLY)
    file(READ "/home/jiping/FENGSim/toolkit/CEM/palace-0.13.0/palace/build/CMakeFiles/git-data/packed-refs" PACKED_REFS)
    if(PACKED_REFS MATCHES "([0-9a-z]*) ${HEAD_REF}")
      set(HEAD_HASH "${CMAKE_MATCH_1}")
    endif()
  endif()
else()  # detached HEAD
  configure_file("/home/jiping/FENGSim/toolkit/CEM/.git/HEAD" "/home/jiping/FENGSim/toolkit/CEM/palace-0.13.0/palace/build/CMakeFiles/git-data/head-ref" COPYONLY)
endif()
if(NOT HEAD_HASH)
  file(READ "/home/jiping/FENGSim/toolkit/CEM/palace-0.13.0/palace/build/CMakeFiles/git-data/head-ref" HEAD_HASH LIMIT 1024)
  string(STRIP "${HEAD_HASH}" HEAD_HASH)
endif()
