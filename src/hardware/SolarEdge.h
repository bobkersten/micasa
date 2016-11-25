#pragma once

#include "../Hardware.h"
#include "../Network.h"

namespace micasa {

	class SolarEdge final : public Hardware, public Worker {

	public:
		SolarEdge( const std::string id_, const std::string reference_, std::string name_ ) : Hardware( id_, reference_, name_ ) { };
		~SolarEdge() { };

		void start() override;
		void stop() override;
		bool updateDevice( const Device::UpdateSource source_, std::shared_ptr<Device> device_, bool& apply_ ) { return true; };

	protected:
		std::chrono::milliseconds _work( const unsigned long int iteration_ );
		
	private:
		void _processHttpReply( mg_connection* connection_, const http_message* message_ );
		
	}; // class SolarEdge

}; // namespace micasa
