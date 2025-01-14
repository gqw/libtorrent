//
//  UUID.h
//  network
//
//  Created by Maoxu Li on 2/11/12.
//  Copyright (c) 2012 LIM Labs. All rights reserved.
//

#ifndef NETWORK_UUID_H
#define NETWORK_UUID_H

#include "Network.h"
#include <string>

#if defined(_WIN32)
#	include <assert.h>
#   include <Rpc.h>
#   pragma comment(lib, "Rpcrt4.lib")
#else
#   include <uuid/uuid.h>
#endif

NETWORK_BEGIN

//
// UUID is a 128 bit identifier.
//

#if defined(_WIN32)

#undef  uuid_t
#define uuid_t GUID

struct UUID
{
	uuid_t data; // struct {long, short, short, char}
    
    UUID()
    {
        
    }
    
    UUID(const char* b, size_t n)
    {
        assert(n >= sizeof(uuid_t));
		memcpy(reinterpret_cast<char*>(&data), b, sizeof(uuid_t));
    }
    
    UUID(const uuid_t id)
	: data(id)
    {
		
    }
    
    UUID(const UUID &other)
	: data(other.data)
    {
        
    }
    
    static UUID New()
    {
        UUID id;
		UuidCreate(&id.data);
        return id;
    }
    
    void Generate()
    {
		UuidCreate(&data);
    }
    
    const char* bytes() const
    {
		return (const char*)(&data);
    }
    
    size_t size() const
    {
        return sizeof(uuid_t);
    }
    
    bool operator==(const UUID &other)
    {
		RPC_STATUS rs;
		return UuidCompare(&data, const_cast<uuid_t*>(&other.data), &rs) == 0;
    }
    
    bool operator!=(const UUID &other)
    {
		RPC_STATUS rs;
		return UuidCompare(&data, const_cast<uuid_t*>(&other.data), &rs) != 0;
    }
    
    std::string toString() const
    {
		char* b;
		UuidToString(&data, (RPC_CSTR *)&b);
		std::string s = b;
		RpcStringFree((RPC_CSTR *)&b);
		return s;
    }
};

#else

struct UUID
{
    uuid_t data; // char [16]
    
    UUID()
    {
        uuid_clear(data);
    }
    
    UUID(const char* b, size_t n)
    {
        uuid_copy(data, const_cast<char*>(b));
    }
    
    UUID(const uuid_t id)
    {
        uuid_copy(data, id);
    }
    
    UUID(const UUID& other)
    {
        uuid_copy(data, other.data);
    }
    
    static UUID New()
    {
        UUID id;
        uuid_generate(id.data);
        return id;
    }
    
    void Generate()
    {
        uuid_generate(data);
    }
    
    const char* bytes() const
    {
        return data;
    }
    
    size_t size() const
    {
        return sizeof(uuid_t);
    }
    
    bool operator==(const UUID &other)
    {
        return uuid_compare(data, other.data) == 0;
    }
    
    bool operator!=(const UUID &other)
    {
        return uuid_compare(data, other.data) != 0;
    }
    
    std::string toString() const
    {
        uuid_string_t s;
        uuid_unparse(data, s);
		return std::string(s);
    }
};

#endif

NETWORK_END

#endif
