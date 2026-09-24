#include "PCH.h"
#include "Version.h"

SKSE_PLUGIN_VERSION = []() {
	SKSE::PluginVersionData v;
	v.PluginVersion(REL::Version{ Version::MAJOR, Version::MINOR, Version::PATCH });
	v.PluginName( Version::NAME.data() );
	v.AuthorName( Version::AUTHOR.data() );
	v.UsesAddressLibrary();
	v.UsesUpdatedStructs();
	v.CompatibleVersions({ SKSE::RUNTIME_SSE_1_5_97, SKSE::RUNTIME_SSE_1_6_1170, SKSE::RUNTIME_SSE_1_7_104 });

	return v;
}();

namespace
{
	static void SKSEMessageHandler( SKSE::MessagingInterface::Message* a_message ) 
	{
		switch( a_message->type )
		{
		case SKSE::MessagingInterface::kDataLoaded:
			REX::INFO( "{} {} loaded", Version::PROJECT.data(), Version::NAME.data());
			break;
		}
	}
}

SKSEPluginLoad( const SKSE::LoadInterface* a_skse )
{
	SKSE::Init( a_skse, { .log = true,
		.logName = Version::PROJECT.data(),
		.trampoline = true,
		.trampolineSize = 450 } );

	if( auto* messaging = SKSE::GetMessagingInterface() )
	{
		messaging->RegisterListener( SKSEMessageHandler );
	}

	return true;
}
