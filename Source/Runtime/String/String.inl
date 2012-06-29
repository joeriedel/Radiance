// String.inl
// Copyright (c) 2010 Sunside Inc., All Rights Reserved
// Author: Joe Riedel
// See Radiance/LICENSE for licensing terms.

#include <algorithm>
#include "../PushSystemMacros.h"

namespace string {

///////////////////////////////////////////////////////////////////////////////

template <typename Traits>
inline CharBuf<Traits>::CharBuf() : m_zone(&ZString.Get()) {
}

template <typename Traits>
inline CharBuf<Traits>::CharBuf(const SelfType &buf) : m_data(buf.m_data), m_zone(buf.m_zone) {
}

template <typename Traits>
inline CharBuf<Traits>::CharBuf(const details::DataBlock::Ref &data, ::Zone &zone) : m_data(data), m_zone(&zone) {
}

template <typename Traits>
inline typename CharBuf<Traits>::SelfType &CharBuf<Traits>::operator = (const SelfType &buf) {
	m_data = buf.m_data;
	m_zone = buf.m_zone;
	return *this;
}

template <typename Traits>
inline bool CharBuf<Traits>::operator == (const SelfType &buf) const {
	if (!m_data || !buf.m_data)
		return false;
	if (m_data->data != buf.m_data->data)
		return false;
	if (m_data->size != buf.m_data->size)
		return false;
	return true;
}

template <typename Traits>
inline bool CharBuf<Traits>::operator != (const SelfType &buf) const {
	return !(*this == buf);
}

template <typename Traits>
inline const typename CharBuf<Traits>::T *CharBuf<Traits>::RAD_IMPLEMENT_GET(c_str) {
	static U32 s_null(0);
	return (m_data) ? (const T*)m_data->data.get() : (const T*)&s_null;
}

template <typename Traits>
inline const typename CharBuf<Traits>::T *CharBuf<Traits>::RAD_IMPLEMENT_GET(begin) {
	return c_str.get();
}

template <typename Traits>
inline const typename CharBuf<Traits>::T *CharBuf<Traits>::RAD_IMPLEMENT_GET(end) {
	return c_str.get() + size.get();
}

template <typename Traits>
inline int CharBuf<Traits>::RAD_IMPLEMENT_GET(size) {
	return (m_data) ? (m_data->size - sizeof(T)) : 0;
}

template <typename Traits>
inline bool CharBuf<Traits>::RAD_IMPLEMENT_GET(empty) {
	return (m_data) ? false : true;
}

template<typename Traits>
inline int CharBuf<Traits>::RAD_IMPLEMENT_GET(numChars) {
	return size.get() / sizeof(T);
}

template <typename Traits>
inline void CharBuf<Traits>::Free() {
	m_data.reset();
}

template <typename Traits>
inline typename CharBuf<Traits>::SelfType CharBuf<Traits>::New(const T *data, int size, const CopyTag_t&, ::Zone &zone) {
	RAD_ASSERT(data);
	return CharBuf(
		details::DataBlock::New(kRefType_Copy, data, size, zone),
		zone
	);
}

template <typename Traits>
inline typename CharBuf<Traits>::SelfType CharBuf<Traits>::New(const T *data, int size, const RefTag_t&, ::Zone &zone) {
	RAD_ASSERT(data);
	return CharBuf(
		details::DataBlock::New(kRefType_Ref, data, size, zone),
		zone
	);
}

///////////////////////////////////////////////////////////////////////////////

inline String::String(::Zone &zone) : m_zone(&zone) {
}

inline String::String(const String &s) : m_data(s.m_data), m_zone(s.m_zone) {
}

inline String::String(const UTF8Buf &buf) : m_data(buf.m_data), m_zone(buf.m_zone) {
}

inline String::String(const UTF16Buf &buf, ::Zone &zone) : m_zone(&zone) {
	m_data = details::DataBlock::New(buf.c_str.get(), buf.numChars, zone);
}

inline String::String(const UTF32Buf &buf, ::Zone &zone) : m_zone(&zone) {
	m_data = details::DataBlock::New(buf.c_str.get(), buf.numChars, zone);
}

inline String::String(const WCharBuf &buf, ::Zone &zone) : m_zone(&zone) {
	m_data = details::DataBlock::New(buf.c_str.get(), buf.numChars, zone);
}

inline String::String(const char *sz, ::Zone &zone) : m_zone(&zone) {
	RAD_ASSERT(sz);
	if (sz[0])
		m_data = details::DataBlock::New(kRefType_Copy, 0, sz, len(sz) + 1, zone);
}

inline String::String(const char *sz, const RefTag_t&, ::Zone &zone) : m_zone(&zone) {
	RAD_ASSERT(sz);
	if (sz[0])
		m_data = details::DataBlock::New(kRefType_Ref, 0, sz, len(sz) + 1, zone);
}

inline String::String(const char *sz, int len, const CopyTag_t&, ::Zone &zone) : m_zone(&zone) {
	RAD_ASSERT(sz);
	if ((len>0) && sz[0]) {
		m_data = details::DataBlock::New(kRefType_Copy, len + 1, sz, len, zone);
		reinterpret_cast<char*>(m_data->m_buf)[len] = 0;
	}
}

inline String::String(const char *sz, int len, const RefTag_t&, ::Zone &zone) : m_zone(&zone) {
	RAD_ASSERT(sz);
	if ((len>0) && sz[0]) {
		m_data = details::DataBlock::New(kRefType_Copy, len + 1, sz, len, zone);
		reinterpret_cast<char*>(m_data->m_buf)[len] = 0;
	}
}

#if defined(RAD_NATIVE_WCHAR_T_DEFINED)

inline String::String(const wchar_t *sz, ::Zone &zone) : m_zone(&zone) {
	RAD_ASSERT(sz);
	if (sz[0])
		m_data = details::DataBlock::New((const WCharTraits::TT*)sz, len(sz), zone);
}

inline String::String(const wchar_t *sz, int len, ::Zone &zone) : m_zone(&zone) {
	RAD_ASSERT(sz);
	if ((len>0) && sz[0])
		m_data = details::DataBlock::New((const WCharTraits::TT*)sz, len, zone);
}

#endif

inline String::String(const U16 *sz, ::Zone &zone) : m_zone(&zone) {
	RAD_ASSERT(sz);
	if (sz[0])
		m_data = details::DataBlock::New(sz, len(sz), zone);
}

inline String::String(const U16 *sz, int len, ::Zone &zone) : m_zone(&zone) {
	RAD_ASSERT(sz);
	if ((len>0) && sz[0])
		m_data = details::DataBlock::New(sz, len, zone);
}

inline String::String(const U32 *sz, ::Zone &zone) : m_zone(&zone) {
	RAD_ASSERT(sz);
	if (sz[0])
		m_data = details::DataBlock::New(sz, len(sz), zone);
}

inline String::String(const U32 *sz, int len, ::Zone &zone) : m_zone(&zone) {
	RAD_ASSERT(sz);
	if ((len>0) && sz[0])
		m_data = details::DataBlock::New(sz, len, zone);
}

inline String::String(char c, ::Zone &zone) : m_zone(&zone) {
	*this = String(&c, 1, CopyTag, zone);
}

#if defined(RAD_NATIVE_WCHAR_T_DEFINED)
inline String::String(wchar_t c, ::Zone &zone) : m_zone(&zone) {
	*this = String(&c, 1, zone);
}
#endif

inline String::String(U16 c, ::Zone &zone) : m_zone(&zone) {
	*this = String(&c, 1, zone);
}

inline String::String(U32 c, ::Zone &zone) : m_zone(&zone) {
	*this = String(&c, 1, zone);
}

inline String::String(const std::string &str, ::Zone &zone) : m_zone(&zone) {
	*this = str.c_str();
}

inline String::String(const std::wstring &str, ::Zone &zone) : m_zone(&zone) {
	*this = str.c_str();
}

inline String::String(int len, ::Zone &zone) : m_zone(&zone) {
	if (len>0) {
		m_data = details::DataBlock::New(kRefType_Copy, len, 0, 0, zone);
		reinterpret_cast<char*>(m_data->m_buf)[len-1] = 0;
	}
}

inline UTF8Buf String::ToUTF8() const {
	UTF8Buf buf;
	buf.m_data = m_data;
	buf.m_zone = m_zone;
	return buf;
}

inline WCharBuf String::ToWChar() const {
#if defined(RAD_OPT_4BYTE_WCHAR)
	UTF32Buf x = ToUTF32();
#else
	UTF16Buf x = ToUTF16();
#endif
	return WCharBuf(x.m_data, *m_zone);
}

inline std::string String::ToStdString() const {
	return std::string(c_str.get());
}

inline std::wstring String::ToStdWString() const {
	WCharBuf x = ToWChar();
	return std::wstring(x.c_str.get());
}

inline int String::Compare(const String &str) const {
	return Compare(str.c_str.get());
}

inline int String::Compare(const char *sz) const {
	RAD_ASSERT(sz);
	return cmp(c_str.get(), sz);
}

inline int String::Compare(const wchar_t *sz) const {
	RAD_ASSERT(sz);
	return Compare(String(sz, *m_zone));
}

inline int String::Comparei(const String &str) const {
	return Comparei(str.c_str.get());
}

inline int String::Comparei(const char *sz) const {
	RAD_ASSERT(sz);
	return icmp(c_str.get(), sz);
}

inline int String::Comparei(const wchar_t *sz) const {
	RAD_ASSERT(sz);
	return Compare(String(sz, *m_zone));
}

inline int String::NCompare(const String &str, int len) const {
	return NCompare(str.c_str.get(), len);
}

inline int String::NCompare(const char *sz, int len) const {
	RAD_ASSERT(sz);
	return ncmp(c_str.get(), sz, len);
}

inline int String::NCompare(const wchar_t *sz, int len) const {
	RAD_ASSERT(sz);
	int mblen = wcstombslen(sz, len);
	return NCompare(String(sz, *m_zone), mblen);
}

inline int String::NComparei(const String &str, int len) const {
	return NComparei(str.c_str.get(), len);
}

inline int String::NComparei(const char *sz, int len) const {
	RAD_ASSERT(sz);
	return nicmp(c_str.get(), sz, len);
}

inline int String::NComparei(const wchar_t *sz, int len) const {
	RAD_ASSERT(sz);
	int mblen = wcstombslen(sz, len);
	return NComparei(String(sz, *m_zone), mblen);
}

inline int String::StrStr(const String &str) const {
	return StrStr(str.c_str.get());
}

inline int String::StrStr(const char *sz) const {
	RAD_ASSERT(sz);
	const char *root = c_str;
	const char *pos = string::strstr(root, sz);
	return pos ? (pos-root) : -1;
}

inline String String::Join(const String &str) const {
	String x(*this);
	x.Append(str);
	return x;
}

inline String String::Join(const char *sz) const {
	String x(*this);
	x.Append(sz);
	return x;
}

inline String String::Join(const wchar_t *sz) const {
	String x(*this);
	x.Append(sz);
	return x;
}

inline String String::Join(const char c) const {
	String x(*this);
	x.Append(c);
	return x;
}

inline String String::Join(const wchar_t c) const {
	String x(*this);
	x.Append(c);
	return x;
}

inline String String::NJoin(const String &str, int len) const {
	String x(*this);
	x.NAppend(str, len);
	return x;
}

inline String String::NJoin(const char *sz, int len) const {
	String x(*this);
	x.NAppend(sz, len);
	return x;
}

inline String String::NJoin(const wchar_t *sz, int len) const {
	String x(*this);
	x.NAppend(sz, len);
	return x;
}

inline String String::SubStrBytes(int first, int count) const {
	RAD_ASSERT(first < length);
	RAD_ASSERT((first+count) <= length);

	return String(
		c_str.get() + first,
		count,
		CopyTag,
		*m_zone
	);
}

inline String String::SubStr(int ofs) const {
	int x = numChars - ofs;
	if (x < 0)
		return String();
	return Right(x);
}

inline String String::SubStrBytes(int ofs) const {
	int x = length - ofs;
	if (x < 0)
		return String();
	return RightBytes(x);
}

inline String String::Left(int count) const {
	return SubStr(0, count);
}

inline String String::Right(int count) const {
	int ofs = numChars - count;
	return SubStr(ofs, count);
}

inline String String::LeftBytes(int count) const {
	return SubStrBytes(0, count);
}

inline String String::RightBytes(int count) const {
	int ofs = length - count;
	return SubStrBytes(ofs, count);
}

inline bool String::operator == (const String &str) const {
	return Compare(str) == 0;
}

inline bool String::operator == (const char *sz) const {
	return Compare(sz) == 0;
}

inline bool String::operator == (const wchar_t *sz) const {
	return Compare(sz) == 0;
}

inline bool String::operator != (const String &str) const {
	return Compare(str) != 0;
}

inline bool String::operator != (const char *sz) const {
	return Compare(sz) != 0;
}

inline bool String::operator != (const wchar_t *sz) const {
	return Compare(sz) != 0;
}

inline bool String::operator > (const String &str) const {
	return Compare(str) > 0;
}

inline bool String::operator > (const char *sz) const {
	return Compare(sz) > 0;
}

inline bool String::operator > (const wchar_t *sz) const {
	return Compare(sz) > 0;
}

inline bool String::operator >= (const String &str) const {
	return Compare(str) >= 0;
}

inline bool String::operator >= (const char *sz) const {
	return Compare(sz) >= 0;
}

inline bool String::operator >= (const wchar_t *sz) const {
	return Compare(sz) >= 0;
}

inline bool String::operator < (const String &str) const {
	return Compare(str) < 0;
}

inline bool String::operator < (const char *sz) const {
	return Compare(sz) < 0;
}

inline bool String::operator < (const wchar_t *sz) const {
	return Compare(sz) < 0;
}

inline bool String::operator <= (const String &str) const {
	return Compare(str) <= 0;
}

inline bool String::operator <= (const char *sz) const {
	return Compare(sz) <= 0;
}

inline bool String::operator <= (const wchar_t *sz) const {
	return Compare(sz) <= 0;
}

inline char String::operator [] (int ofs) const {
	return reinterpret_cast<const char*>(m_data->data.get())[ofs];
}

inline bool String::EqualsInstance(const String &str) const {
	return m_data && str.m_data && m_data->data == str.m_data->data;
}

inline bool String::EqualsInstance(const UTF8Buf &buf) const {
	return m_data && buf.m_data && m_data->data == buf.m_data->data;
}

inline String &String::UpperASCII() {
	if (m_data) {
		m_data = details::DataBlock::Isolate(m_data, *m_zone);
		toupper(reinterpret_cast<char*>(m_data->data.get()));
	}
	return *this;
}

inline String &String::LowerASCII() {
	if (m_data) {
		m_data = details::DataBlock::Isolate(m_data, *m_zone);
		tolower((char*)m_data->data.get());
	}
	return *this;
}

inline String &String::ReverseASCII() {
	if (m_data) {
		m_data = details::DataBlock::Isolate(m_data, *m_zone);
		std::reverse((char*)m_data->data.get(), ((char*)m_data->data.get()) + m_data->size);
	}
	return *this;
}

inline String &String::TrimSubStr(int ofs, int count) {
	*this = SubStr(ofs, count);
	return *this;
}

inline String &String::TrimSubStrBytes(int ofs, int count) {
	*this = SubStrBytes(ofs, count);
	return *this;
}

inline String &String::TrimLeft(int count) {
	*this = Left(count);
	return *this;
}

inline String &String::TrimRight(int count) {
	*this = Right(count);
	return *this;
}

inline String &String::TrimLeftBytes(int count) {
	*this = LeftBytes(count);
	return *this;
}

inline String &String::TrimRightBytes(int count) {
	*this = RightBytes(count);
	return *this;
}

inline String &String::Append(const String &str) {
	return NAppend(str, str.length);
}

inline String &String::Append(const char *sz) {
	return Append(String(sz, RefTag));
}

inline String &String::Append(const wchar_t *sz) {
	return Append(String(sz, *m_zone));
}

inline String &String::Append(const char c) {
	char x[2] = {c, 0};
	return Append(x);
}

inline String &String::Append(const wchar_t c) {
	wchar_t x[2] = {c, 0};
	return Append(x);
}

inline String &String::NAppend(const String &str, int len) {
	return NAppendBytes(str, str.ByteForChar(len));
}

inline String &String::NAppend(const char *sz, int len) {
	return NAppend(String(sz, RefTag), len);
}

inline String &String::NAppend(const wchar_t *sz, int len) {
	return NAppend(String(sz, *m_zone), len);
}

inline String &String::NAppendBytes(const char *sz, int len) {
	return NAppendBytes(String(sz, RefTag), len);
}

inline String &String::NAppendBytes(const wchar_t *sz, int len) {
	return NAppendBytes(String(sz, *m_zone), len);
}

inline String &String::Replace(char src, char dst) {
	char x[] = {src, 0};
	char y[] = {dst, 0};
	return Replace(x, y);
}

inline String &String::Replace(char src, const char *dst) {
	char x[] = {src, 0};
	return Replace(x, dst);
}

inline String &String::Replace(char src, wchar_t dst) {
	char x[] = {src, 0};
	wchar_t y[] = {dst, 0};
	return Replace(x, y);
}

inline String &String::Replace(char src, const wchar_t *dst) {
	char x[] = {src, 0};
	return Replace(x, dst);
}

inline String &String::Replace(const char *src, char dst) {
	char y[] = {dst, 0};
	return Replace(src, y);
}

inline String &String::Replace(const char *src, const char *dst) {
	return Replace(String(src, RefTag), String(dst, RefTag));
}

inline String &String::Replace(const char *src, wchar_t dst) {
	wchar_t y[] = {dst, 0};
	return Replace(src, y);
}

inline String &String::Replace(const char *src, const wchar_t *dst) {
	return Replace(String(src, RefTag), String(dst));
}

inline String &String::Replace(wchar_t src, char dst) {
	wchar_t x[] = {src, 0};
	char y[] = {dst, 0};
	return Replace(x, y);
}

inline String &String::Replace(wchar_t src, const char *dst) {
	wchar_t x[] = {src, 0};
	return Replace(x, dst);
}

inline String &String::Replace(wchar_t src, wchar_t dst) {
	wchar_t x[] = {src, 0};
	wchar_t y[] = {dst, 0};
	return Replace(x, y);
}

inline String &String::Replace(wchar_t src, const wchar_t *dst) {
	wchar_t x[] = {src, 0};
	return Replace(x, dst);
}

inline String &String::Replace(const wchar_t *src, char dst) {
	char y[] = {dst, 0};
	return Replace(src, y);
}

inline String &String::Replace(const wchar_t *src, const char *dst) {
	return Replace(String(src), String(dst, RefTag));
}

inline String &String::Replace(const wchar_t *src, wchar_t dst) {
	wchar_t y[] = {dst, 0};
	return Replace(src, y);
}

inline String &String::Replace(const wchar_t *src, const wchar_t *dst) {
	return Replace(String(src), String(dst));
}

inline String &String::Printf(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	Printf(fmt, args);
	va_end(args);
	return *this;
}

inline String &String::PrintfASCII(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	PrintfASCII(fmt, args);
	va_end(args);
	return *this;
}

inline String &String::operator = (const String &string) {
	m_data = string.m_data;
	m_zone = string.m_zone;
	return *this;
}

inline String &String::operator = (const char *sz) {
	return (*this = String(sz));
}

inline String &String::operator = (const wchar_t *sz) {
	return (*this = String(sz));
}

inline String &String::operator = (char c) {
	return (*this = String(c));
}

inline String &String::operator = (wchar_t c) {
	return (*this = String(c));
}

inline String &String::operator += (const String &string) {
	return Append(string);
}

inline String &String::operator += (const char *sz) {
	return Append(sz);
}

inline String &String::operator += (const wchar_t *sz) {
	return Append(sz);
}

inline String &String::operator += (char c) {
	return Append(c);
}

inline String &String::operator += (wchar_t c) {
	return Append(c);
}

inline String &String::Write(int pos, char sz) {
	return Write(pos, &sz, 1);
}

inline String &String::Write(int pos, const char *sz) {
	return Write(pos, sz, len(sz)+1);
}

inline String &String::Write(int pos, const String &str) {
	return Write(pos, str.c_str, str.length);
}

inline String &String::Write(int pos, const String &str, int len) {
	return Write(pos, str.c_str, len);
}

inline String &String::Clear() {
	m_data.reset();
	return *this;
}

inline String operator + (const String &a, const String &b) {
	String x(a);
	x.Append(b);
	return x;
}

inline String operator + (const String &a, const char *sz) {
	String x(a);
	x.Append(sz);
	return x;
}

inline String operator + (const String &a, const wchar_t *sz) {
	String x(a);
	x.Append(sz);
	return x;
}

inline String operator + (const char *sz, const String &b) {
	String x(sz, RefTag);
	x.Append(b);
	return x;
}

inline String operator + (const wchar_t *sz, const String &b) {
	String x(sz);
	x.Append(b);
	return x;
}

inline String operator + (char s, const String &b) {
	String x(s);
	x.Append(b);
	return x;
}

inline String operator + (wchar_t s, const String &b) {
	String x(s);
	x.Append(b);
	return x;
}

inline String operator + (const String &b, char s) {
	String x(b);
	x.Append(s);
	return x;
}

inline String operator + (const String &b, wchar_t s) {
	String x(b);
	x.Append(s);
	return x;
}

inline int String::RAD_IMPLEMENT_GET(length) {
	return m_data ? (m_data->size - 1) : 0;
}

inline const char *String::RAD_IMPLEMENT_GET(begin) {
	return c_str.get();
}

inline const char *String::RAD_IMPLEMENT_GET(end) {
	return c_str.get() + length.get();
}

inline const char *String::RAD_IMPLEMENT_GET(c_str) {
	return m_data ? (const char*)m_data->data.get() : "";
}

inline int String::RAD_IMPLEMENT_GET(numChars) {
	return m_data ? utf8to32len((const char*)m_data->data.get(), m_data->size - 1) : 0;
}

inline bool String::RAD_IMPLEMENT_GET(empty) {
	return !m_data;
}

template<class CharType, class Traits>
std::basic_istream<CharType, Traits>& operator >> (std::basic_istream<CharType, Traits> &stream, String &string) {
	std::string x;
	stream >> x;
	string = x.c_str();
	return stream;
}


template<class CharType, class Traits>
std::basic_ostream<CharType, Traits>& operator << (std::basic_ostream<CharType, Traits> &stream, const String &string) {
	stream << string.c_str.get();
	return stream;
}

} // string

inline string::String CStr(const char *sz) {
	return string::String(sz, string::RefTag);
}

#include "../PopSystemMacros.h"
