#include <endian.h>
#include <type_traits>

template<typename T>
static inline typename std::enable_if<std::is_integral<T>::value, T>::type hton(T value) {
	static_assert(sizeof(uint8_t) <= sizeof(value) && sizeof(value) <= sizeof(uint64_t), "Bad type for hton<T>");
	switch (sizeof(value)) {
		case sizeof(uint8_t):
			return value;
		case sizeof(uint16_t):
			return T(htobe16(uint16_t(value)));
		case sizeof(uint32_t):
			return T(htobe32(uint32_t(value)));
		case sizeof(uint64_t):
			return T(htobe64(uint64_t(value)));
		default:
			return 0;
	}
}

template<typename T>
static inline typename std::enable_if<std::is_integral<T>::value, T>::type ntoh(T value) {
	static_assert(sizeof(uint8_t) <= sizeof(value) && sizeof(value) <= sizeof(uint64_t), "Bad type for ntoh<T>");
	switch (sizeof(value)) {
		case sizeof(uint8_t):
			return value;
		case sizeof(uint16_t):
			return T(be16toh(uint16_t(value)));
		case sizeof(uint32_t):
			return T(be32toh(uint32_t(value)));
		case sizeof(uint64_t):
			return T(be64toh(uint64_t(value)));
		default:
			return 0;
	}
}

