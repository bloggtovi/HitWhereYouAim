#pragma once

#include <Windows.h>
#include <DbgHelp.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#pragma comment( lib, "Dbghelp.lib" )

struct PDBSymbolLookupContext
{
	const char* requestedName = nullptr;
	DWORD64 moduleBase = 0;
	std::uintptr_t address = 0;
};

struct PDBMemberResult
{
	ULONG typeIndex = 0;
	std::uint32_t offset = 0;
};

static bool PDBSymbolNameMatches( const char* a_name, const char* a_requestedName )
{
	if( !a_name || !a_requestedName )
		return false;

	if( std::strcmp( a_name, a_requestedName ) == 0 )
		return true;

	std::string_view name( a_name );
	std::string_view requestedName( a_requestedName );

	if( name.size() < requestedName.size() )
		return false;

	if( name.substr( name.size() - requestedName.size() ) == requestedName )
		return true;

	std::size_t parameterPosition = name.find( '(' );

	if( parameterPosition != std::string_view::npos )
		name = name.substr( 0, parameterPosition );

	std::size_t callingConventionPosition = name.rfind( "__cdecl " );

	if( callingConventionPosition != std::string_view::npos )
		name.remove_prefix( callingConventionPosition + 8 );

	return name == requestedName;
}

static BOOL CALLBACK FindPDBSymbolCallback( SYMBOL_INFO* a_symbol, ULONG, void* a_context )
{
	if( !a_symbol || !a_context )
		return TRUE;

	auto context = static_cast<PDBSymbolLookupContext*>( a_context );

	if( a_symbol->ModBase != context->moduleBase )
		return TRUE;

	if( PDBSymbolNameMatches( a_symbol->Name, context->requestedName ) )
	{
		context->address = static_cast<std::uintptr_t>( a_symbol->Address );
		return FALSE;
	}

	std::array<char, MAX_SYM_NAME> undecoratedName{};

	DWORD length = UnDecorateSymbolName(
		a_symbol->Name,
		undecoratedName.data(),
		static_cast<DWORD>( undecoratedName.size() ),
		UNDNAME_COMPLETE );

	if( length && PDBSymbolNameMatches( undecoratedName.data(), context->requestedName ) )
	{
		context->address = static_cast<std::uintptr_t>( a_symbol->Address );
		return FALSE;
	}

	return TRUE;
}

static BOOL CALLBACK LogPDBSymbolCallback( SYMBOL_INFO* a_symbol, ULONG, void* )
{
	if( !a_symbol )
		return TRUE;

	std::array<char, MAX_SYM_NAME> undecoratedName{};

	DWORD length = UnDecorateSymbolName(
		a_symbol->Name,
		undecoratedName.data(),
		static_cast<DWORD>( undecoratedName.size() ),
		UNDNAME_COMPLETE );

	REX::DEBUG(
		"PDB symbol: name=\"{}\", undecorated=\"{}\", address={:016X}, size={}, tag={}, flags={:X}",
		a_symbol->Name,
		length ? undecoratedName.data() : "",
		a_symbol->Address,
		a_symbol->Size,
		a_symbol->Tag,
		a_symbol->Flags );

	return TRUE;
}

static std::optional<std::uint32_t> GetLoadedModuleSize( HMODULE a_module )
{
	if( !a_module )
		return std::nullopt;

	auto bytes = reinterpret_cast<const std::byte*>( a_module );
	auto dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>( bytes );

	if( dosHeader->e_magic != IMAGE_DOS_SIGNATURE )
		return std::nullopt;

	auto ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS*>( bytes + dosHeader->e_lfanew );

	if( ntHeaders->Signature != IMAGE_NT_SIGNATURE )
		return std::nullopt;

	return ntHeaders->OptionalHeader.SizeOfImage;
}

static bool EnsurePDBLoaded( HMODULE a_module )
{
	if( !a_module )
		return false;

	static std::mutex symbolMutex;
	static std::vector<HMODULE> loadedModules;

	std::scoped_lock lock( symbolMutex );

	if( std::find( loadedModules.begin(), loadedModules.end(), a_module ) != loadedModules.end() )
		return true;

	std::array<wchar_t, 32768> modulePath{};

	DWORD modulePathLength = GetModuleFileNameW(
		a_module,
		modulePath.data(),
		static_cast<DWORD>( modulePath.size() ) );

	if( modulePathLength == 0 || modulePathLength >= modulePath.size() )
		return false;

	std::filesystem::path imagePath( modulePath.data() );
	std::filesystem::path pdbPath = imagePath.parent_path() / L"SmoothCam.pdb";

	if( !std::filesystem::exists( pdbPath ) )
	{
		REX::DEBUG( "PDB file is missing: {}", pdbPath.string() );
		return false;
	}

	auto moduleSize = GetLoadedModuleSize( a_module );

	if( !moduleSize )
		return false;

	HANDLE process = GetCurrentProcess();

	static bool symbolHandlerInitialized = false;

	if( !symbolHandlerInitialized )
	{
		DWORD options = SymGetOptions();

		options &= ~SYMOPT_DEFERRED_LOADS;
		options &= ~SYMOPT_EXACT_SYMBOLS;
		options &= ~SYMOPT_PUBLICS_ONLY;
		options &= ~SYMOPT_UNDNAME;

		options |= SYMOPT_FAIL_CRITICAL_ERRORS;
		options |= SYMOPT_LOAD_ANYTHING;
		options |= SYMOPT_LOAD_LINES;
		options |= SYMOPT_NO_PROMPTS;

		SymSetOptions( options );

		if( !SymInitializeW( process, imagePath.parent_path().c_str(), FALSE ) )
		{
			if( GetLastError() != ERROR_INVALID_PARAMETER )
			{
				REX::DEBUG( "SymInitializeW failed: {}", GetLastError() );
				return false;
			}
		}

		symbolHandlerInitialized = true;
	}

	DWORD64 moduleBase = reinterpret_cast<DWORD64>( a_module );

	if( SymGetModuleBase64( process, moduleBase ) == moduleBase )
	{
		loadedModules.push_back( a_module );
		return true;
	}

	SetLastError( ERROR_SUCCESS );

	DWORD64 loadedBase = SymLoadModuleExW(
		process,
		nullptr,
		pdbPath.c_str(),
		imagePath.stem().c_str(),
		moduleBase,
		*moduleSize,
		nullptr,
		0 );

	if( !loadedBase )
	{
		DWORD error = GetLastError();

		if( error != ERROR_SUCCESS )
		{
			REX::DEBUG( "SymLoadModuleExW failed for {}: {}", pdbPath.string(), error );
			return false;
		}

		loadedBase = moduleBase;
	}

	REX::DEBUG( "Loaded PDB {} at {:016X}", pdbPath.string(), loadedBase );

	loadedModules.push_back( a_module );
	return true;
}

[[maybe_unused]] static void LogPDBSymbols( HMODULE a_module, const char* a_mask = "*" )
{
	if( !a_module || !EnsurePDBLoaded( a_module ) )
		return;

	if( !a_mask || !a_mask[0] )
		a_mask = "*";

	HANDLE process = GetCurrentProcess();
	DWORD64 moduleBase = reinterpret_cast<DWORD64>( a_module );

	SetLastError( ERROR_SUCCESS );

	BOOL result = SymEnumSymbols(
		process,
		moduleBase,
		a_mask,
		LogPDBSymbolCallback,
		nullptr );

	if( !result && GetLastError() != ERROR_SUCCESS )
		REX::DEBUG( "SymEnumSymbols failed: {}", GetLastError() );
}

[[maybe_unused]] static std::optional<std::uintptr_t> GetPDBSymbolAddress(
	HMODULE a_module,
	const char* a_symbol )
{
	if( !a_module || !a_symbol || !a_symbol[0] )
		return std::nullopt;

	if( !EnsurePDBLoaded( a_module ) )
		return std::nullopt;

	HANDLE process = GetCurrentProcess();
	DWORD64 moduleBase = reinterpret_cast<DWORD64>( a_module );

	std::array<wchar_t, 32768> modulePath{};

	if( !GetModuleFileNameW( a_module, modulePath.data(), static_cast<DWORD>( modulePath.size() ) ) )
		return std::nullopt;

	std::filesystem::path path( modulePath.data() );
	std::string qualifiedSymbol = path.stem().string() + "!" + a_symbol;

	alignas( SYMBOL_INFO ) std::array<std::byte, sizeof( SYMBOL_INFO ) + MAX_SYM_NAME> symbolBuffer{};
	auto symbolInfo = reinterpret_cast<SYMBOL_INFO*>( symbolBuffer.data() );

	symbolInfo->SizeOfStruct = sizeof( SYMBOL_INFO );
	symbolInfo->MaxNameLen = MAX_SYM_NAME;

	if( SymFromName( process, qualifiedSymbol.c_str(), symbolInfo ) && symbolInfo->ModBase == moduleBase )
		return static_cast<std::uintptr_t>( symbolInfo->Address );

	if( SymFromName( process, a_symbol, symbolInfo ) && symbolInfo->ModBase == moduleBase )
		return static_cast<std::uintptr_t>( symbolInfo->Address );

	PDBSymbolLookupContext context{
		a_symbol,
		moduleBase,
		0
	};

	SetLastError( ERROR_SUCCESS );

	SymEnumSymbols(
		process,
		moduleBase,
		"*",
		FindPDBSymbolCallback,
		&context );

	if( context.address )
		return context.address;

	REX::DEBUG( "Failed to find PDB symbol {}: {}", qualifiedSymbol, GetLastError() );
	return std::nullopt;
}

static std::optional<PDBMemberResult> FindPDBMember(
	HMODULE a_module,
	ULONG a_parentTypeIndex,
	std::string_view a_memberName )
{
	if( !a_module || !a_parentTypeIndex || a_memberName.empty() )
		return std::nullopt;

	HANDLE process = GetCurrentProcess();
	DWORD64 moduleBase = reinterpret_cast<DWORD64>( a_module );
	ULONG childCount = 0;

	if( !SymGetTypeInfo( process, moduleBase, a_parentTypeIndex, TI_GET_CHILDRENCOUNT, &childCount ) )
		return std::nullopt;

	if( childCount == 0 )
		return std::nullopt;

	std::size_t storageSize = sizeof( TI_FINDCHILDREN_PARAMS );

	if( childCount > 1 )
		storageSize += sizeof( ULONG ) * static_cast<std::size_t>( childCount - 1 );

	std::vector<std::byte> storage( storageSize );
	auto children = reinterpret_cast<TI_FINDCHILDREN_PARAMS*>( storage.data() );

	children->Count = childCount;
	children->Start = 0;

	if( !SymGetTypeInfo( process, moduleBase, a_parentTypeIndex, TI_FINDCHILDREN, children ) )
		return std::nullopt;

	for( ULONG i = 0; i < childCount; i++ )
	{
		WCHAR* wideName = nullptr;

		if( !SymGetTypeInfo( process, moduleBase, children->ChildId[i], TI_GET_SYMNAME, &wideName ) )
			continue;

		std::array<char, MAX_SYM_NAME> name{};

		WideCharToMultiByte(
			CP_UTF8,
			0,
			wideName,
			-1,
			name.data(),
			static_cast<int>( name.size() ),
			nullptr,
			nullptr );

		LocalFree( wideName );

		if( a_memberName != name.data() )
			continue;

		DWORD offset = 0;
		ULONG memberTypeIndex = 0;

		if( !SymGetTypeInfo( process, moduleBase, children->ChildId[i], TI_GET_OFFSET, &offset ) )
			return std::nullopt;

		if( !SymGetTypeInfo( process, moduleBase, children->ChildId[i], TI_GET_TYPEID, &memberTypeIndex ) )
			memberTypeIndex = children->ChildId[i];

		return PDBMemberResult{
			memberTypeIndex,
			static_cast<std::uint32_t>( offset )
		};
	}

	return std::nullopt;
}
