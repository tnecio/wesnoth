/*
	Copyright (C) 2024
	Part of the Battle for Wesnoth Project https://www.wesnoth.org/

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY.

	See the COPYING file for more details.
*/

/**
 * @file
 * Compatibility shim: maps boost::filesystem API to std::filesystem for
 * Emscripten (WASM) builds, where Boost.Filesystem is not available.
 *
 * Include this instead of <boost/filesystem.hpp> etc. when __EMSCRIPTEN__ is
 * defined.  The shim patches the two main API differences:
 *   - file_type enumerators: boost uses bare names (regular_file, directory_file)
 *     while std uses scoped enum values (file_type::regular, file_type::directory).
 *   - last_write_time: boost returns std::time_t; std returns file_time_type.
 *
 * boost::system::error_code is aliased to std::error_code.
 * BOOST_IOSTREAMS_FAILURE is defined as std::ios_base::failure.
 */

#pragma once

#include <boost/format.hpp>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace boost {
namespace system {
	using error_code = std::error_code;
	using errc       = std::errc;
} // namespace system

namespace filesystem {

using path                       = std::filesystem::path;
using directory_iterator         = std::filesystem::directory_iterator;
using recursive_directory_iterator = std::filesystem::recursive_directory_iterator;
using directory_entry            = std::filesystem::directory_entry;
using file_status                = std::filesystem::file_status;
using ifstream                   = std::ifstream;
using ofstream                   = std::ofstream;

// Boost exposes file_type enumerators directly in the namespace.
// std::filesystem scopes them inside file_type.
constexpr auto regular_file   = std::filesystem::file_type::regular;
constexpr auto directory_file = std::filesystem::file_type::directory;
constexpr auto symlink_file   = std::filesystem::file_type::symlink;

// Delegate the free functions that share identical signatures.
using std::filesystem::exists;
using std::filesystem::is_directory;
using std::filesystem::is_regular_file;
using std::filesystem::is_symlink;
using std::filesystem::create_directory;
using std::filesystem::create_directories;
using std::filesystem::remove;
using std::filesystem::rename;
using std::filesystem::file_size;
using std::filesystem::status;
using std::filesystem::symlink_status;
using std::filesystem::absolute;
using std::filesystem::canonical;
using std::filesystem::copy_file;
using std::filesystem::read_symlink;
using std::filesystem::relative;
using std::filesystem::temp_directory_path;
using std::filesystem::current_path;

// last_write_time wrappers: boost returns std::time_t; convert from file_time_type.
inline std::time_t last_write_time(const path& p)
{
	auto ftime = std::filesystem::last_write_time(p);
	// Convert file_time_type → system_clock::time_point via clock difference trick.
	auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
		ftime - std::filesystem::file_time_type::clock::now()
		      + std::chrono::system_clock::now());
	return std::chrono::system_clock::to_time_t(sctp);
}

inline std::time_t last_write_time(const path& p, std::error_code& ec)
{
	auto ftime = std::filesystem::last_write_time(p, ec);
	if(ec) return std::time_t(-1);
	auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
		ftime - std::filesystem::file_time_type::clock::now()
		      + std::chrono::system_clock::now());
	return std::chrono::system_clock::to_time_t(sctp);
}

} // namespace filesystem
} // namespace boost

// Used in ostream_file to catch Boost.Iostreams write errors.
// Under Emscripten it maps to the equivalent std exception.
#define BOOST_IOSTREAMS_FAILURE std::ios_base::failure
