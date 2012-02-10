/* -*-c++-*- libcoin - Copyright (C) 2012 Michael Gronager
 *
 * libcoin is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * libcoin is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with libcoin.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef MESSAGE_HEADER_H
#define MESSAGE_HEADER_H

#include <coin/uint256.h>
#include <coin/serialize.h>
#include <coinChain/Chain.h>

#include <string>

#include <boost/array.hpp>

//
// Message header
//  (4) message start
//  (12) command
//  (4) size
//  (4) checksum

//extern unsigned char pchMessageStart[4];

class MessageHeader
{
public:
    MessageHeader();
    //    MessageHeader(const Chain& chain);
    //    MessageHeader(const char* pszCommand, unsigned int nMessageSizeIn);
    MessageHeader(const Chain& chain, const char* pszCommand, unsigned int nMessageSizeIn);
    
    std::string GetCommand() const;
    bool IsValid(const Chain& chain) const;
    
    IMPLEMENT_SERIALIZE
    (
     READWRITE(FLATDATA(_messageStart));
     READWRITE(FLATDATA(pchCommand));
     READWRITE(nMessageSize);
     if (nVersion >= 209)
     READWRITE(nChecksum);
     )
    
    // TODO: make private (improves encapsulation)
public:
    enum { COMMAND_SIZE=12 };
    MessageStart _messageStart;
    char pchCommand[COMMAND_SIZE];
    unsigned int nMessageSize;
    unsigned int nChecksum;
};

#endif // MESSAGE_HEADER_H