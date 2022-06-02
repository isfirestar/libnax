#if !defined NSP_EXCEPTION_H
#define NSP_EXCEPTION_H

#include <string>
#include <cstdint>

namespace nsp {
	namespace toolkit {
		class base_exception {
		public:
			base_exception() {}
			base_exception( const std::string &fr ) : fr_( fr ) {}
			base_exception( int code ) : code_( code ) {}
			base_exception( const base_exception &lref ) {
				fr_ = lref.fr_;
				code_ = lref.code_;
			}
			base_exception( base_exception &&rref ) {
				fr_ = std::move( rref.fr_ );
				code_ = rref.code_;
				rref.code_ = 0;
			}
			base_exception &operator=( const base_exception &lref ) {
				if ( &lref != this ) {
					fr_ = lref.fr_;
					code_ = lref.code_;
				}
				return *this;
			}
			base_exception &operator=( base_exception &&rref ) {
				fr_ = std::move( rref.fr_ );
				code_ = rref.code_;
				rref.code_ = 0;
				return *this;
			}
			const std::string &why() const { return fr_; }
			const int how() const { return code_; }
		protected:
			std::string fr_;
			int code_;
		};
	}
}

#endif