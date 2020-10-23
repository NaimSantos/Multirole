#include "HornetWrapper.hpp"

#include <spdlog/spdlog.h> // TODO: remove

#include "IDataSupplier.hpp"
#include "IScriptSupplier.hpp"
#include "ILogger.hpp"
#include "../../HornetCommon.hpp"
#include "../../Process.hpp"

namespace Ignis::Multirole::Core
{

#include "../../Read.inl"
#include "../../Write.inl"

inline std::string MakeHornetName(uintptr_t addr)
{
	std::array<char, 25U> buf;
	std::snprintf(buf.data(), buf.size(), "Hornet0x%lX", addr);
	// Make sure the shared memory object doesn't exist before attempting
	// to create it again.
	ipc::shared_memory_object::remove(buf.data());
	return std::string(buf.data());
}

inline ipc::shared_memory_object MakeShm(const std::string& str)
{
	ipc::shared_memory_object shm(ipc::create_only, str.data(), ipc::read_write);
	shm.truncate(sizeof(Hornet::SharedSegment));
	return shm;
}

// public

HornetWrapper::HornetWrapper(std::string_view absFilePath) :
	shmName(MakeHornetName(reinterpret_cast<uintptr_t>(this))),
	shm(MakeShm(shmName)),
	region(shm, ipc::read_write),
	hss(nullptr)
{
	void* addr = region.get_address();
	hss = new (addr) Hornet::SharedSegment();
	Process::Launch("./hornet", absFilePath.data(), shmName.data());
	spdlog::info("Hornet launched!");
}

HornetWrapper::~HornetWrapper()
{
	// TODO: signal closure and wait for hornet to send goodbye message
	ipc::shared_memory_object::remove(shmName.data());
}

std::pair<int, int> HornetWrapper::Version()
{
	auto lock = NotifyAndWaitCompletion(Hornet::Action::OCG_GET_VERSION);
	const auto* rptr = hss->bytes.data();
	return
	{
		Read<int>(rptr),
		Read<int>(rptr),
	};
}

IWrapper::Duel HornetWrapper::CreateDuel(const DuelOptions& opts)
{
	return OCG_Duel{nullptr};
}

void HornetWrapper::DestroyDuel(Duel duel)
{
	auto* wptr = hss->bytes.data();
	Write<OCG_Duel>(wptr, duel);
	auto lock = NotifyAndWaitCompletion(Hornet::Action::OCG_DESTROY_DUEL);
}

void HornetWrapper::AddCard(Duel duel, const OCG_NewCardInfo& info)
{}

void HornetWrapper::Start(Duel duel)
{
	auto* wptr = hss->bytes.data();
	Write<OCG_Duel>(wptr, duel);
	auto lock = NotifyAndWaitCompletion(Hornet::Action::OCG_START_DUEL);
}

IWrapper::DuelStatus HornetWrapper::Process(Duel duel)
{
	return DuelStatus::DUEL_STATUS_END;
}

IWrapper::Buffer HornetWrapper::GetMessages(Duel duel)
{
	auto* wptr = hss->bytes.data();
	Write<OCG_Duel>(wptr, duel);
	auto lock = NotifyAndWaitCompletion(Hornet::Action::OCG_DUEL_GET_MESSAGE);
	const auto* rptr = hss->bytes.data();
	const auto size = static_cast<std::size_t>(Read<uint32_t>(rptr));
	Buffer buffer(size);
	std::memcpy(buffer.data(), rptr, size);
	return buffer;
}

void HornetWrapper::SetResponse(Duel duel, const Buffer& buffer)
{
	auto* wptr = hss->bytes.data();
	Write<OCG_Duel>(wptr, duel);
	Write<std::size_t>(wptr, buffer.size());
	std::memcpy(wptr, buffer.data(), buffer.size());
	auto lock = NotifyAndWaitCompletion(Hornet::Action::OCG_DUEL_SET_RESPONSE);
}

int HornetWrapper::LoadScript(Duel duel, std::string_view name, std::string_view str)
{
	return 1;
}

std::size_t HornetWrapper::QueryCount(Duel duel, uint8_t team, uint32_t loc)
{
	auto* wptr = hss->bytes.data();
	Write<OCG_Duel>(wptr, duel);
	Write<uint8_t>(wptr, team);
	Write<uint32_t>(wptr, loc);
	auto lock = NotifyAndWaitCompletion(Hornet::Action::OCG_DUEL_QUERY_COUNT);
	const auto* rptr = hss->bytes.data();
	return static_cast<std::size_t>(Read<uint32_t>(rptr));
}

IWrapper::Buffer HornetWrapper::Query(Duel duel, const QueryInfo& info)
{
	auto* wptr = hss->bytes.data();
	Write<OCG_Duel>(wptr, duel);
	Write<OCG_QueryInfo>(wptr, info);
	auto lock = NotifyAndWaitCompletion(Hornet::Action::OCG_DUEL_QUERY);
	const auto* rptr = hss->bytes.data();
	const auto size = static_cast<std::size_t>(Read<uint32_t>(rptr));
	Buffer buffer(size);
	std::memcpy(buffer.data(), rptr, size);
	return buffer;
}

IWrapper::Buffer HornetWrapper::QueryLocation(Duel duel, const QueryInfo& info)
{
	auto* ptr1 = hss->bytes.data();
	Write<OCG_Duel>(ptr1, duel);
	Write<OCG_QueryInfo>(ptr1, info);
	auto lock = NotifyAndWaitCompletion(Hornet::Action::OCG_DUEL_QUERY_LOCATION);
	const auto* rptr = hss->bytes.data();
	const auto size = static_cast<std::size_t>(Read<uint32_t>(rptr));
	Buffer buffer(size);
	std::memcpy(buffer.data(), rptr, size);
	return buffer;
}

IWrapper::Buffer HornetWrapper::QueryField(Duel duel)
{
	auto* ptr1 = hss->bytes.data();
	Write<OCG_Duel>(ptr1, duel);
	auto lock = NotifyAndWaitCompletion(Hornet::Action::OCG_DUEL_QUERY_FIELD);
	const auto* rptr = hss->bytes.data();
	auto size = static_cast<std::size_t>(Read<uint32_t>(rptr));
	Buffer buffer(size);
	std::memcpy(buffer.data(), rptr, size);
	return buffer;
}

HornetWrapper::LockType HornetWrapper::NotifyAndWaitCompletion(Hornet::Action act)
{
	hss->act = act;
	LockType lock(hss->mtx);
	hss->cv.notify_one();
	hss->cv.wait(lock, [&](){return hss->act == Hornet::Action::NO_WORK;});
	return lock;
}

} // namespace Ignis::Multirole::Core
