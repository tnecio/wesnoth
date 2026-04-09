# store the specified sources list in the specified variable
function(GetSources source_list store_in_var)
	file(STRINGS "${CMAKE_SOURCE_DIR}/source_lists/${source_list}" sources)
	foreach(source IN LISTS sources)
	    list(APPEND loaded_sources "${CMAKE_SOURCE_DIR}/src/${source}")
		set(${store_in_var} ${loaded_sources} PARENT_SCOPE)
	endforeach()
endfunction()

# external git submodules
add_subdirectory("modules")

## some includes ##
include_directories(SYSTEM ${PANGOCAIRO_INCLUDE_DIRS})
include_directories(SYSTEM ${GETTEXT_INCLUDE_DIR})
include_directories(SYSTEM ${LIBDBUS_INCLUDE_DIRS})
if(NOT MSVC)
	include_directories(SYSTEM ${SDL2IMAGE_INCLUDE_DIRS})
	include_directories(SYSTEM ${SDL2MIXER_INCLUDE_DIRS})
endif()

if(ZLIB_INCLUDE_DIR)
	include_directories(SYSTEM ${ZLIB_INCLUDE_DIR} )
endif()

# needed to get include paths in the subfolders correct
include_directories( ${CMAKE_SOURCE_DIR}/src/ )
# needed to have the generated config.h used, too
include_directories( ${CMAKE_CURRENT_BINARY_DIR} )

link_directories(
	${PANGOCAIRO_LIBRARY_DIRS}
	${LIBDBUS_LIBRARY_DIRS}
)

set(common-external-libs ${ICU_DATA_LIBRARY} ${ICU_I18N_LIBRARY} ${ICU_UC_LIBRARY})
if(MSVC)
	set(common-external-libs ${common-external-libs} shlwapi.lib winmm.lib crypt32.lib)
elseif(MINGW)
	set(common-external-libs ${common-external-libs} wsock32 ws2_32 shlwapi winmm crypt32)
elseif(APPLE)
	set(common-external-libs ${common-external-libs} ${APPKIT_LIBRARY} ${FOUNDATION_LIBRARY} ${IOKIT_LIBRARY} ${SECURITY_LIBRARY})
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "SunOS")
	set(common-external-libs ${common-external-libs} socket)
endif()

if(ENABLE_SYSTEM_LUA)
	add_compile_definitions(HAVE_SYSTEM_LUA=1)
	include_directories( SYSTEM ${LUA_INCLUDE_DIR} )
	set(common-external-libs ${common-external-libs} ${LUA_LIBRARIES})
endif()

set(game-external-libs
	${common-external-libs}
	${PANGOCAIRO_LIBRARIES}
	${LIBDBUS_LIBRARIES}
	VorbisFile::VorbisFile
)

if(NOT MSVC)
	set(game-external-libs
		${game-external-libs}
		${SDL2IMAGE_LIBRARIES}
		${SDL2MIXER_LIBRARIES}
	)
endif()

if(HISTORY_FOUND)
	set(game-external-libs ${game-external-libs} History::History)
endif()

# get source lists
GetSources("libwesnoth_sdl" wesnoth_sdl_sources)
GetSources("libwesnoth_widgets" wesnoth_widget-sources)
GetSources("libwesnoth" wesnoth_game_sources)
GetSources("libwesnoth_core" wesnoth_core_sources)
GetSources("wesnoth" wesnoth_sources)

if(WIN32)
	set(wesnoth_core_sources ${wesnoth_core_sources} log_windows.cpp)
	set(wesnoth_game_sources ${wesnoth_game_sources} desktop/windows_tray_notification.cpp desktop/windows_battery_info.cpp)
endif()

# On apple only
if(APPLE)
	set(wesnoth_game_sources
		${wesnoth_game_sources}
		desktop/apple_battery_info.mm
		desktop/apple_notification.mm
		desktop/apple_version.mm
		desktop/apple_video.mm
	)
endif()

# For libdbus (essentially just for linux), this file needs to be linked, as its header is included #ifdef HAVE_LIBDBUS
if(LIBDBUS_FOUND)
	set(wesnoth_game_sources ${wesnoth_game_sources} desktop/dbus_features.cpp)
endif()

# Depending on the flag create a real file or an empty dummy.
#
# Since the code is executed every build run of Wesnoth we need to make sure
# that no modifications don't turn into a rebuild. Therefore a dummy is
# created and the second target only copies the file if modified.
if(ENABLE_DISPLAY_REVISION)
	add_custom_target(wesnoth-revision_dummy
		COMMAND ${CMAKE_SOURCE_DIR}/utils/autorevision.sh -t h > ${CMAKE_CURRENT_BINARY_DIR}/revision.dummy
		WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
	)

	add_custom_target(wesnoth-revision
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			${CMAKE_CURRENT_BINARY_DIR}/revision.dummy
			${CMAKE_CURRENT_BINARY_DIR}/revision.h
	)

	add_dependencies(wesnoth-revision wesnoth-revision_dummy)
	set_source_files_properties(game_version.cpp PROPERTIES COMPILE_DEFINITIONS "LOAD_REVISION")
endif()

########### Conf Tests ###########

if((ENABLE_GAME OR ENABLE_TESTS) AND NOT MSVC)
	# test for SDL2
	add_executable(sdl2 conftests/sdl2.cpp)
	set_target_properties(sdl2 PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/Testing)
	target_link_libraries(sdl2 ${game-external-libs})
	# cmake checks the version elsewhere already, but scons uses this, which is why the three arguments for major.minor.patchlevel are 0 here
	add_test(NAME SDL2_SUPPORT COMMAND sdl2 0 0 0)

	# test for SDL2_image
	add_executable(sdl2_image conftests/sdl2_image.cpp)
	set_target_properties(sdl2_image PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/Testing)
	target_link_libraries(sdl2_image ${game-external-libs})
	add_test(NAME SDL2_IMAGE_SUPPORT COMMAND sdl2_image)

	# test for SDL2_mixer
	add_executable(sdl2_mixer conftests/sdl2_mixer.cpp)
	set_target_properties(sdl2_mixer PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/Testing)
	target_link_libraries(sdl2_mixer ${game-external-libs})
	add_test(NAME SDL2_MIXER_SUPPORT COMMAND sdl2_mixer)

	# test for JPG support in SDL2
	add_executable(sdl2_jpg conftests/sdl2_jpg.cpp)
	set_target_properties(sdl2_jpg PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/Testing)
	target_link_libraries(sdl2_jpg ${game-external-libs})
	add_test(NAME SDL2_JPG_SUPPORT COMMAND sdl2_jpg "${CMAKE_SOURCE_DIR}/data/core/images/scons_conftest_images/end-n.jpg")

	# test for PNG support in SDL2
	add_executable(sdl2_png conftests/sdl2_png.cpp)
	set_target_properties(sdl2_png PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/Testing)
	target_link_libraries(sdl2_png ${game-external-libs})
	add_test(NAME SDL2_PNG_SUPPORT COMMAND sdl2_png "${CMAKE_SOURCE_DIR}/data/core/images/scons_conftest_images/end-n.png")

	# test for WEBP support in SDL2
	add_executable(sdl2_webp conftests/sdl2_webp.cpp)
	set_target_properties(sdl2_webp PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/Testing)
	target_link_libraries(sdl2_webp ${game-external-libs})
	add_test(NAME SDL2_WEBP_SUPPORT COMMAND sdl2_webp "${CMAKE_SOURCE_DIR}/data/core/images/scons_conftest_images/end-n.webp")

	# test for audio support in SDL2
	add_executable(sdl2_audio conftests/sdl2_audio.cpp)
	set_target_properties(sdl2_audio PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/Testing)
	target_link_libraries(sdl2_audio ${game-external-libs})
	add_test(NAME SDL2_AUDIO_SUPPORT COMMAND sdl2_audio "${CMAKE_SOURCE_DIR}/data/core/music/main_menu.ogg")

	add_custom_target(conftests
		COMMAND ${CMAKE_COMMAND} -E env "SDL_AUDIODRIVER=dummy" ${CMAKE_CTEST_COMMAND}
		DEPENDS
			sdl2
			sdl2_image
			sdl2_mixer
			sdl2_jpg
			sdl2_png
			sdl2_webp
			sdl2_audio
	)
endif()

########### Wesnoth ###############

add_library(wesnoth-common STATIC ${wesnoth_core_sources})
if(ENABLE_GAME OR ENABLE_TESTS)
	add_library(wesnoth-client STATIC ${wesnoth_sources} ${wesnoth_game_sources} ${wesnoth_sdl_sources})
	if(NOT ENABLE_SYSTEM_LUA)
    	target_link_libraries(wesnoth-client lua)
	endif()
	# widgets need special handling since otherwise the way they're registered causes the linker to remove them since it incorrectly thinks they're unused
	add_library(wesnoth-widgets STATIC ${wesnoth_widget-sources})
	if(APPLE)
		set(WIDGETS_LIB -Wl,-force_load wesnoth-widgets)
	elseif(NOT MSVC)
		set(WIDGETS_LIB -Wl,--whole-archive wesnoth-widgets -Wl,--no-whole-archive)
	else()
		# handled by /WHOLEARCHIVE below
		set(WIDGETS_LIB wesnoth-widgets)
	endif()
endif()

if(ENABLE_GAME)
	if(WIN32)
		add_executable(wesnoth WIN32 wesnoth.cpp ../packaging/windows/wesnoth.rc)
		if(MSVC)
			target_link_options(wesnoth PRIVATE /WX /WHOLEARCHIVE:wesnoth-widgets)
		endif()
	elseif(APPLE)
		add_executable(wesnoth wesnoth.cpp macosx/SDLMain.mm)
	else()
		add_executable(wesnoth wesnoth.cpp)
	endif()

	target_link_libraries(wesnoth
		${WIDGETS_LIB}
		wesnoth-client
		wesnoth-common
		${game-external-libs}
		OpenSSL::Crypto
		OpenSSL::SSL
		${Boost_LIBRARIES}
		Fontconfig::Fontconfig
		SDL2::SDL2
		SDL2::SDL2main
		CURL::libcurl
	)
	if(MSVC)
		target_link_libraries(wesnoth SDL2_image::SDL2_image)
		target_link_libraries(wesnoth SDL2_mixer::SDL2_mixer)
	endif()

	if(ENABLE_DISPLAY_REVISION)
		add_dependencies(wesnoth-common wesnoth-revision) # game_version.cpp, where LOAD_REVISION is used, is in wesnoth-common
	endif()

	set_target_properties(wesnoth PROPERTIES OUTPUT_NAME ${BINARY_PREFIX}wesnoth${BINARY_SUFFIX})

	install(TARGETS wesnoth DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

########### Boost Unit tests ###############

if(ENABLE_TESTS)
	add_definitions(-DBOOST_TEST_DYN_LINK)

	GetSources("boost_unit_tests" boost_tests_sources)
	add_executable(boost_unit_tests ${boost_tests_sources})

	if(MSVC)
		target_link_options(boost_unit_tests PRIVATE /WX /WHOLEARCHIVE:wesnoth-widgets)
	endif()

	target_link_libraries(boost_unit_tests
		${WIDGETS_LIB}
		wesnoth-client
		wesnoth-common
		${game-external-libs}
		OpenSSL::Crypto
		OpenSSL::SSL
		${Boost_LIBRARIES}
		Fontconfig::Fontconfig
		SDL2::SDL2
		SDL2::SDL2main
		CURL::libcurl
	)
	if(MSVC)
		target_link_libraries(boost_unit_tests SDL2_image::SDL2_image)
		target_link_libraries(boost_unit_tests SDL2_mixer::SDL2_mixer)
	endif()

	if(ENABLE_DISPLAY_REVISION)
		add_dependencies(boost_unit_tests wesnoth-revision)
	endif()

	set_target_properties(boost_unit_tests PROPERTIES OUTPUT_NAME ${BINARY_PREFIX}boost_unit_tests${BINARY_SUFFIX})
endif()

########### Wesnothd Server ###############

if(ENABLE_SERVER)
	GetSources("wesnothd" wesnothd_sources)

	if(WIN32)
		add_executable(wesnothd ${wesnothd_sources} ../packaging/windows/wesnothd.rc)
	else()
		add_executable(wesnothd ${wesnothd_sources})
	endif()

	if(ENABLE_MYSQL)
		target_include_directories(wesnothd SYSTEM PRIVATE ${MYSQL_CFLAGS})
		target_compile_definitions(wesnothd PRIVATE HAVE_MYSQLPP)
		target_link_libraries(wesnothd mariadbclientpp)
	endif()

	target_link_libraries(wesnothd
		wesnoth-common
		${common-external-libs}
		${MYSQL_LIBS}
		OpenSSL::Crypto
		OpenSSL::SSL
		${Boost_LIBRARIES}
	)
	if(MSVC)
		target_link_options(wesnothd PRIVATE /WX)
	endif()

	set_target_properties(wesnothd PROPERTIES OUTPUT_NAME ${BINARY_PREFIX}wesnothd${BINARY_SUFFIX})
	if(ENABLE_DISPLAY_REVISION)
		add_dependencies(wesnothd wesnoth-revision)
	endif()

	install(TARGETS wesnothd DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

########### WesnothLite Shared Library / WASM Module ###############

GetSources("wesnoth_headless" wesnoth_headless_sources)
GetSources("wesnoth_headless_stubs" wesnoth_headless_stubs_sources)
GetSources("lua" lua_headless_sources)

# Stubs are compiled as a separate OBJECT library so that editing stub files
# or changing source_lists/wesnoth_headless_stubs only invalidates stub objects,
# not the ~250 main game source files.
add_library(wesnothlite-stubs OBJECT ${wesnoth_headless_stubs_sources})
target_include_directories(wesnothlite-stubs BEFORE PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/wesnothlite/stubs
	${CMAKE_CURRENT_SOURCE_DIR}/wesnothlite/headless
)
target_include_directories(wesnothlite-stubs PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_BINARY_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/wesnothlite
	${CMAKE_CURRENT_SOURCE_DIR}/modules
)

target_compile_definitions(wesnothlite-stubs PRIVATE HEADLESS_ENGINE=1)
set_property(TARGET wesnothlite-stubs PROPERTY POSITION_INDEPENDENT_CODE ON)
if(EMSCRIPTEN)
	target_compile_options(wesnothlite-stubs PRIVATE
		-sUSE_ICU=1 -sUSE_ZLIB=1 -sUSE_BZIP2=1 -fexceptions -pthread)
endif()

if(EMSCRIPTEN)
	add_executable(wesnothlite
		${CMAKE_CURRENT_SOURCE_DIR}/wesnothlite/wesnoth_engine.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/wesnothlite/wl_bindings.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/wesnothlite/wl_snapshots.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/wesnothlite/wl_lua.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/wesnothlite/wl_controller.cpp
		${wesnoth_headless_sources}
		${lua_headless_sources}
		$<TARGET_OBJECTS:wesnothlite-stubs>
	)
	# Emscripten outputs .js + .wasm pair; set the output name explicitly.
	set_target_properties(wesnothlite PROPERTIES OUTPUT_NAME wesnothlite SUFFIX ".js")
	target_compile_options(wesnothlite PRIVATE
		-sUSE_ICU=1
		-sUSE_ZLIB=1
		-sUSE_BZIP2=1
		-fexceptions
		-pthread
	)
	target_link_options(wesnothlite PRIVATE
		--bind
		-sMODULARIZE=1
		-sEXPORT_ES6=1
		-sEXPORT_NAME=WesnothLite
		-sEXPORTED_RUNTIME_METHODS=FS
		-sFORCE_FILESYSTEM=1
		-lnodefs.js
		-lidbfs.js
		-sALLOW_MEMORY_GROWTH=1
		-sSTACK_SIZE=5242880
		-sINITIAL_MEMORY=67108864
		-sWASM=1
		-sUSE_SDL=2
		-sUSE_ICU=1
		-sUSE_ZLIB=1
		-sUSE_BZIP2=1
		-fexceptions
		-sDISABLE_EXCEPTION_CATCHING=0
		-sEXIT_RUNTIME=0
		-sNO_EXIT_RUNTIME=1
		-pthread
		-sPTHREAD_POOL_SIZE=2
		-sPROXY_TO_PTHREAD=0
		-sEXPORTED_FUNCTIONS=
		-sASSERTIONS=1
		-O1
	)
	target_link_libraries(wesnothlite
		${common-external-libs}
		# boost::filesystem replaced by std::filesystem via compat header
		${Boost_LIBRARIES}
		${CMAKE_DL_LIBS}
	)
else()
	add_library(wesnothlite SHARED
		${CMAKE_CURRENT_SOURCE_DIR}/wesnothlite/wesnothlite.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/wesnothlite/wesnoth_engine.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/wesnothlite/wl_snapshots.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/wesnothlite/wl_events.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/wesnothlite/wl_lua.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/wesnothlite/wl_controller.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/wesnothlite/wl_queries.cpp
		${wesnoth_headless_sources}
		${lua_headless_sources}
		$<TARGET_OBJECTS:wesnothlite-stubs>
	)
	target_link_libraries(wesnothlite
		${common-external-libs}
		${Boost_LIBRARIES}
		OpenSSL::Crypto
		OpenSSL::SSL
		${CMAKE_DL_LIBS}
	)
endif()

# Stubs and mock SDL headers shadow the real ones — BEFORE prepends ahead of global include_directories
target_include_directories(wesnothlite BEFORE PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/wesnothlite/stubs
	${CMAKE_CURRENT_SOURCE_DIR}/wesnothlite/headless
)
target_include_directories(wesnothlite PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_BINARY_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/wesnothlite
	${CMAKE_CURRENT_SOURCE_DIR}/modules
)

target_compile_definitions(wesnothlite PRIVATE HEADLESS_ENGINE=1)
set_property(TARGET wesnothlite PROPERTY POSITION_INDEPENDENT_CODE ON)

# Apply same Lua compile flags as the main build
file(GLOB wesnoth_lua_config ${CMAKE_CURRENT_SOURCE_DIR}/wesnoth_lua_config.h)
set_source_files_properties(${lua_headless_sources} PROPERTIES OBJECT_DEPENDS ${wesnoth_lua_config})
if(NOT MSVC)
	set_source_files_properties(${lua_headless_sources} PROPERTIES
		COMPILE_FLAGS "-include \"${wesnoth_lua_config}\" -x c++ -Wno-old-style-cast -Wno-useless-cast -Wno-stringop-overflow")
else()
	set_source_files_properties(${lua_headless_sources} PROPERTIES COMPILE_FLAGS "/FI\"${wesnoth_lua_config}\"")
endif()

# Force the linker to fail if any symbols are missing
if(EMSCRIPTEN)
	# Emscripten handles undefined symbols differently — rely on its checks.
elseif(APPLE)
	target_link_options(wesnothlite PRIVATE "-Wl,-undefined,error")
else()
	target_link_options(wesnothlite PRIVATE "-Wl,--no-undefined")
endif()

########### WesnothLite CLI (native only) ###############

if(NOT EMSCRIPTEN)
	add_executable(wl-cli
		${CMAKE_CURRENT_SOURCE_DIR}/wesnothlite/wl_cli.cpp
	)

	target_include_directories(wl-cli PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/wesnothlite
	)

	target_link_libraries(wl-cli
		wesnothlite
	)
endif()

########### Campaignd Server ###############

if(ENABLE_CAMPAIGN_SERVER)
	GetSources("campaignd" campaignd_sources)

	add_executable(campaignd ${campaignd_sources})

	if(ENABLE_MYSQL)
		target_include_directories(campaignd SYSTEM PRIVATE ${MYSQL_CFLAGS})
		target_compile_definitions(campaignd PRIVATE HAVE_MYSQLPP)
		target_link_libraries(campaignd mariadbclientpp)
	endif()

	target_link_libraries(
		campaignd
		wesnoth-common
		${common-external-libs}
		${MYSQL_LIBS}
		OpenSSL::Crypto
		OpenSSL::SSL
		${Boost_LIBRARIES}
	)
	if(MSVC)
		target_link_options(campaignd PRIVATE /WX)
	endif()

	set_target_properties(campaignd PROPERTIES OUTPUT_NAME ${BINARY_PREFIX}campaignd${BINARY_SUFFIX})
	if(ENABLE_DISPLAY_REVISION)
		add_dependencies(campaignd wesnoth-revision)
	endif()

	install(TARGETS campaignd DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
