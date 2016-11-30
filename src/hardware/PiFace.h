#pragma once

#include "../Hardware.h"

namespace micasa {

	class PiFace final : public Hardware {

	public:
		PiFace( const std::string id_, const std::string reference_, std::string name_ ) : Hardware( id_, reference_, name_ ) { };
		~PiFace() { };
		
		bool updateDevice( const Device::UpdateSource source_, std::shared_ptr<Device> device_, bool& apply_ ) { return true; };

	protected:
		std::chrono::milliseconds _work( const unsigned long int iteration_ ) { return std::chrono::milliseconds( 1000 ); }

	}; // class PiFace

}; // namespace micasa
