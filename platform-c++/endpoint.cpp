#include <cstdlib>
#include <cstdio>
#include <cstring>

#include "abuff.h"
#include "naos.h"

#include "endpoint.h"
#include "toolkit.h"
#include "os_util.hpp"
#include "swnet.h"
#include "exception.hpp"

namespace nsp {
	namespace tcpip {

		//////////////////////////////////////////////// IP check ////////////////////////////////////////////////
		nsp_boolean_t endpoint::is_effective_ipv4( const std::string &ipstr )
		{
			static const std::string delim = ".";

			if ( 0 == ipstr.length() ) {
				return NO;
			}

			std::string value[4];
			std::size_t loc = 0, offset = 0;

			int i = 0;
			// 切割IP串， 不满足刚好 -- 3个'.'4个节 -- 的串均为非法IPv4
			while ( ( loc = ipstr.find_first_of( delim, offset ) ) != std::string::npos ) {
				if ( i == 3 ) {
					return NO;
				}

				value[i] = ipstr.substr( offset, loc - offset );
				offset = loc + 1;
				i++;
			}
			if ( 3 == i ) {
				value[3] = ipstr.substr( offset );
			} else {
				return NO;
			}

			//对4个value进行判断
			for ( int i = 0; i < 4; i++ ) {
				//全数字校验
				if ( !toolkit::is_digit_str( value[i] ) ) {
					return NO;
				}

				// 每个节的越界判断
				char* endptr;
				auto n_value = strtol( value[i].c_str(), &endptr, 10 );
				if ( n_value > 255 ) {
					return NO;
				}
			}
			return YES;
		}

		nsp_boolean_t endpoint::is_effective_port( const std::string &portstr, uint16_t &port )
		{
			// 全数字校验
			if ( !toolkit::is_digit_str( portstr ) ) {
				return NO;
			}

			// 如果不是指定端口为0(使用随机端口), 第0个字节必须是1-9, 不能是0
			if ( 0x30 == portstr[0] && portstr.size() > 1 ) {
				return NO;
			}

			// 越界判断
			char* endptr;
			auto n_port = strtol( portstr.c_str(), &endptr, 10 );
			if ( n_port >= MAXIMU_TCPIP_PORT_NUMBER ) {
				return NO;
			}
			port = ( uint16_t ) n_port;
			return YES;
		}

		int endpoint::parse_domain( const std::string &domain, std::string &ipv4 )
		{
			uint32_t ip;

			// "localhost" 可以得到正确解析
			if (::nis_gethost( domain.c_str(), &ip ) < 0 ) {
				return -1;
			}

			abuff_naos_inet_t iptxt;
			nsp_status_t status = ::naos_ipv4tos(ip, &iptxt);
			if ( !NSP_SUCCESS(status) ) {
				return -1;
			}
			ipv4.assign( iptxt.u.cst );
			return 0;
		}

		int endpoint::parse_ep( const std::string & epstr, std::string &ipv4, port_t &port )
		{
			static const std::string delim = ":";

			// 区分开ip和port
			std::string hoststr;
			std::string portstr;
			std::size_t loc = 0, offset = 0;

			// 找不到有效的端口分割符
			loc = epstr.find( delim, offset );
			if ( ( 0 == loc ) || ( std::string::npos == loc ) ) {
				return -1;
			}
			hoststr = epstr.substr( offset, loc - offset );
			offset = loc + 1;
			portstr = epstr.substr( offset );

			// 必须要有host域
			if ( 0 == hoststr.length() ) {
				return -1;
			}

			// 端口必须保证有效
			if ( !endpoint::is_effective_port( portstr, port ) ) {
				return -1;
			}

			// host 域直接是有效的IPv4地址
			// 对IP字符串和端口进行有效性判定
			// 数字的网络地址， 不作 gethostbyaddr 有效性判断
			if ( endpoint::is_effective_ipv4( hoststr ) ) {
				ipv4 = hoststr;
				return 0;
			}

			// 允许解析域名/主机名
			return parse_domain( hoststr, ipv4 );
		}

		endpoint::endpoint()
		{
			ipv4( 0 );
			port( INVALID_TCPIP_PORT_NUMBER );
		}

		endpoint::endpoint( const char *ipstr, const port_t po )
		{
			char epstr[128];
			if ( 0 == strlen( ipstr ) ) {
				crt_sprintf( epstr, cchof( epstr ), "0.0.0.0:%u", po );
			} else {
				crt_sprintf( epstr, cchof( epstr ), "%s:%u", ipstr, po );
			}
			if ( endpoint::build( epstr, *this ) < 0 ) {
				throw toolkit::base_exception( "failed build endpoint." );
			}
		}

		endpoint::endpoint( const endpoint &rf )
		{
			if ( &rf != this ) {
				crt_strcpy( ipstr_, cchof( ipstr_ ), rf.ipstr_ );
				address_ = rf.address_;
				port_ = rf.port_;
			}
		}

		endpoint::endpoint( endpoint &&rf )
		{
			crt_strcpy( ipstr_, cchof( ipstr_ ), rf.ipstr_ );
			memset( rf.ipstr_, 0, cchof( rf.ipstr_ ) );
			address_ = rf.address_;
			rf.address_ = 0;
			port_ = rf.port_;
			rf.port_ = 0;
		}

		endpoint &endpoint::operator=( endpoint &&rf )
		{
			crt_strcpy( ipstr_, cchof( ipstr_ ), rf.ipstr_ );
			address_ = rf.address_;
			port_ = rf.port_;
			return *this;
		}

		endpoint &endpoint::operator=( const endpoint &rf )
		{
			if ( &rf != this ) {
				ipv4( rf.ipv4() );
				port( rf.port() );
			}
			return *this;
		}

		bool endpoint::operator==( const endpoint &rf ) const
		{
			if ( &rf != this ) {
				return ( ( 0 == strcmp( ipstr_, rf.ipv4() ) ) && address_ == rf.address_ && port_ == rf.port() );
			} else {
				return true;
			}
		}

		bool endpoint::operator<( const endpoint &rf ) const
		{
			if ( address_ < rf.address_ ) {
				return true;
			} else {
				if ( address_ > rf.address_ ) {
					return false;
				} else {
					return port_ < rf.port_;
				}
			}
		}

		endpoint::operator bool() const
		{
			return ( ( 0 != address_ ) ? ( true ) : ( INVALID_TCPIP_PORT_NUMBER != port_ ) );
		}

		const bool endpoint::connectable() const
		{
			return ( ( 0 != address_ ) && ( BOARDCAST_TCPIP_ADDRESS != address_ ) && ( INVALID_TCPIP_PORT_NUMBER != port_ ) );
		}

		const bool endpoint::bindable() const
		{
			return ( ( BOARDCAST_TCPIP_ADDRESS != address_ ) && ( INVALID_TCPIP_PORT_NUMBER != port_ ) );
		}

		const bool endpoint::manual() const
		{
			return ( MANUAL_NOTIFY_TARGET == address_ );
		}

		const char *endpoint::ipv4() const
		{
			return &ipstr_[0];
		}

		const u32_ipv4_t endpoint::ipv4_uint32() const
		{
			return address_;
		}

		void endpoint::ipv4( const std::string &ipstr )
		{
			if ( 0 == ipstr.length() ) {
				return;
			}

			do {
				// 直接是有效的IP地址
				if ( is_effective_ipv4( ipstr ) ) {
					crt_strcpy( ipstr_, cchof( ipstr_ ), ipstr.c_str() );
					break;
				}

				// 可以解析的域名
				std::string domain_ipstr;
				if ( endpoint::parse_domain( ipstr, domain_ipstr ) >= 0 ) {
					crt_strcpy( ipstr_, cchof( ipstr_ ), domain_ipstr.c_str() );
					break;
				}

				return;
			} while ( 0 );

			address_ = ::naos_ipv4tou( ipstr_, kByteOrder_LittleEndian );
		}

		void endpoint::ipv4( const char *ipstr, int cpcch )
		{
			if ( ipstr && cpcch > 0 ) {
				ipv4( std::string( ipstr, cpcch ) );
			}
		}

		void endpoint::ipv4( const u32_ipv4_t uint32_address )
		{
			address_ = uint32_address;
			if ( uint32_address > 0 ) {
				abuff_naos_inet_t iptxt;
				nsp_status_t status = ::naos_ipv4tos(address_, &iptxt);
				if ( NSP_SUCCESS(status) ) {
					crt_strcpy( ipstr_, cchof( ipstr_ ), iptxt.u.cst );
				}
			} else {
				memset( ipstr_, 0, sizeof( ipstr_ ) );
			}
		}

		const port_t endpoint::port() const
		{
			return port_;
		}

		void endpoint::port( const port_t po )
		{
			port_ = po;
		}

		const std::string endpoint::to_string() const
		{
			std::string epstr;
			epstr += ipstr_;
			epstr += ":";
			epstr += toolkit::to_string<char>( port_ );
			return epstr;
		}

		int endpoint::build( const std::string &epstr, endpoint &ep )
		{
			std::string ipstr;
			port_t port;
			if ( endpoint::parse_ep( epstr, ipstr, port ) < 0 ) {
				return -1;
			}
			ep.ipv4( ipstr );
			ep.port( port );
			return 0;
		}

		int endpoint::build( const char *ipstr, uint16_t port, endpoint &ep )
		{
			if ( !ipstr ) {
				return -1;
			}

			char epstr[128];
			crt_sprintf( epstr, cchof( epstr ), "%s:%u", ipstr, port );
			return endpoint::build( std::string( epstr ), ep );
		}

		endpoint endpoint::boardcast( const port_t po )
		{
			if ( 0 == po || INVALID_TCPIP_PORT_NUMBER == po ) {
				return endpoint( "0.0.0.0", INVALID_TCPIP_PORT_NUMBER );
			} else {
				return endpoint( "255.255.255.255", po );
			}
		}

		void endpoint::disable()
		{
			ipv4( ( uint32_t ) 0 );
			port( INVALID_TCPIP_PORT_NUMBER );
		}

	} // namespace tcpip
} // nsp
