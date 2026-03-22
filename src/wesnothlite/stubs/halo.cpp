/* Headless stub: halo.cpp — no-op implementations for halo symbols */
#include "halo.hpp"

namespace halo {

manager::manager() : impl_(nullptr)
{
}

handle manager::add(int /*x*/, int /*y*/, const std::string& /*image*/,
	const map_location& /*loc*/, ORIENTATION /*orientation*/, bool /*infinite*/)
{
	return handle();
}

void manager::set_location(const handle& /*h*/, int /*x*/, int /*y*/)
{
}

void manager::remove(const handle& /*h*/)
{
}

void manager::update()
{
}

void manager::render(const rect& /*r*/)
{
}

halo_record::halo_record()
	: id_(NO_HALO)
	, my_manager_()
{
}

halo_record::halo_record(int id, const std::shared_ptr<halo_impl>& my_manager)
	: id_(id)
	, my_manager_(my_manager)
{
}

halo_record::~halo_record()
{
}

} // namespace halo
