// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2011 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#include "headers.h"
//#include "../../src/cryptopp/sha.h"
#include "db.h"
#include "net.h"
#include "init.h"
#undef printf
#include <boost/asio.hpp>
#include <boost/iostreams/concepts.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/algorithm/string.hpp>
#ifdef USE_SSL
#include <boost/asio/ssl.hpp> 
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
typedef boost::asio::ssl::stream<boost::asio::ip::tcp::socket> SSLStream;
#endif
#include "../../src/json/json_spirit_reader_template.h"
#include "../../src/json/json_spirit_writer_template.h"
#include "../../src/json/json_spirit_utils.h"
#define printf OutputDebugStringF
// MinGW 3.4.5 gets "fatal error: had to relocate PCH" if the json headers are
// precompiled in headers.h.  The problem might be when the pch file goes over
// a certain size around 145MB.  If we need access to json_spirit outside this
// file, we could use the compiled json_spirit option.

using namespace std;
using namespace boost;
using namespace boost::asio;
using namespace json_spirit;

void ThreadRPCServer2(void* parg);
typedef Value(*rpcfn_type)(const Array& params, bool fHelp);
extern map<string, rpcfn_type> mapCallTable;

//static int64 nWalletUnlockTime;
//static CCriticalSection cs_nWalletUnlockTime;


Object JSONRPCError(int code, const string& message)
{
    Object error;
    error.push_back(Pair("code", code));
    error.push_back(Pair("message", message));
    return error;
}


void PrintConsole(const char* format, ...)
{
    char buffer[50000];
    int limit = sizeof(buffer);
    va_list arg_ptr;
    va_start(arg_ptr, format);
    int ret = _vsnprintf(buffer, limit, format, arg_ptr);
    va_end(arg_ptr);
    if (ret < 0 || ret >= limit)
    {
        ret = limit - 1;
        buffer[limit-1] = 0;
    }
    printf("%s", buffer);
#if defined(__WXMSW__) && defined(GUI)
    MyMessageBox(buffer, "Bitcoin", wxOK | wxICON_EXCLAMATION);
#else
    fprintf(stdout, "%s", buffer);
#endif
}


int64 AmountFromValue(const Value& value)
{
    double dAmount = value.get_real();
    if (dAmount <= 0.0 || dAmount > 21000000.0)
        throw JSONRPCError(-3, "Invalid amount");
    int64 nAmount = roundint64(dAmount * COIN);
    if (!MoneyRange(nAmount))
        throw JSONRPCError(-3, "Invalid amount");
    return nAmount;
}

Value ValueFromAmount(int64 amount)
{
    return (double)amount / (double)COIN;
}
/*
void WalletTxToJSON(const CWalletTx& wtx, Object& entry)
{
    entry.push_back(Pair("confirmations", wtx.GetDepthInMainChain()));
    entry.push_back(Pair("txid", wtx.GetHash().GetHex()));
    entry.push_back(Pair("time", (boost::int64_t)wtx.GetTxTime()));
    BOOST_FOREACH(const PAIRTYPE(string,string)& item, wtx.mapValue)
        entry.push_back(Pair(item.first, item.second));
}

string AccountFromValue(const Value& value)
{
    string strAccount = value.get_str();
    if (strAccount == "*")
        throw JSONRPCError(-11, "Invalid account name");
    return strAccount;
}
*/


///
/// Note: This interface may still be subject to change.
///


Value help(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw runtime_error(
            "help [command]\n"
            "List commands, or get help for a command.");

    string strCommand;
    if (params.size() > 0)
        strCommand = params[0].get_str();

    string strRet;
    set<rpcfn_type> setDone;
    for (map<string, rpcfn_type>::iterator mi = mapCallTable.begin(); mi != mapCallTable.end(); ++mi)
    {
        string strMethod = (*mi).first;
        // We already filter duplicates, but these deprecated screw up the sort order
        if (strMethod == "getamountreceived" ||
            strMethod == "getallreceived" ||
            (strMethod.find("label") != string::npos))
            continue;
        if (strCommand != "" && strMethod != strCommand)
            continue;
        try
        {
            Array params;
            rpcfn_type pfn = (*mi).second;
            if (setDone.insert(pfn).second)
                (*pfn)(params, true);
        }
        catch (std::exception& e)
        {
            // Help text is returned in an exception
            string strHelp = string(e.what());
            if (strCommand == "")
                if (strHelp.find('\n') != -1)
                    strHelp = strHelp.substr(0, strHelp.find('\n'));
            strRet += strHelp + "\n";
        }
    }
    if (strRet == "")
        strRet = strprintf("help: unknown command: %s\n", strCommand.c_str());
    strRet = strRet.substr(0,strRet.size()-1);
    return strRet;
}


Value stop(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "stop\n"
            "Stop bitcoin server.");

    // Shutdown will take long enough that the response should get back
    CreateThread(Shutdown, NULL);
    return "bitcoin server stopping";
}


Value getblockcount(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getblockcount\n"
            "Returns the number of blocks in the longest block chain.");

    return nBestHeight;
}


Value getblocknumber(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getblocknumber\n"
            "Returns the block number of the latest block in the longest block chain.");

    return nBestHeight;
}


Value getconnectioncount(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getconnectioncount\n"
            "Returns the number of connections to other nodes.");

    return (int)vNodes.size();
}


double GetDifficulty()
{
    // Floating point number that is a multiple of the minimum difficulty,
    // minimum difficulty = 1.0.

    if (pindexBest == NULL)
        return 1.0;
    int nShift = (pindexBest->nBits >> 24) & 0xff;

    double dDiff =
        (double)0x0000ffff / (double)(pindexBest->nBits & 0x00ffffff);

    while (nShift < 29)
    {
        dDiff *= 256.0;
        nShift++;
    }
    while (nShift > 29)
    {
        dDiff /= 256.0;
        nShift--;
    }

    return dDiff;
}

Value getdifficulty(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getdifficulty\n"
            "Returns the proof-of-work difficulty as a multiple of the minimum difficulty.");

    return GetDifficulty();
}


Value getgenerate(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getgenerate\n"
            "Returns true or false.");

    return (bool)fGenerateBitcoins;
}

/*
Value setgenerate(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "setgenerate <generate> [genproclimit]\n"
            "<generate> is true or false to turn generation on or off.\n"
            "Generation is limited to [genproclimit] processors, -1 is unlimited.");

    bool fGenerate = true;
    if (params.size() > 0)
        fGenerate = params[0].get_bool();

    if (params.size() > 1)
    {
        int nGenProcLimit = params[1].get_int();
        fLimitProcessors = (nGenProcLimit != -1);
        WriteSetting("fLimitProcessors", fLimitProcessors);
        if (nGenProcLimit != -1)
            WriteSetting("nLimitProcessors", nLimitProcessors = nGenProcLimit);
        if (nGenProcLimit == 0)
            fGenerate = false;
    }

    GenerateBitcoins(fGenerate, pwalletMain);
    return Value::null;
}
*/

Value gethashespersec(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "gethashespersec\n"
            "Returns a recent hashes per second performance measurement while generating.");

    if (GetTimeMillis() - nHPSTimerStart > 8000)
        return (boost::int64_t)0;
    return (boost::int64_t)dHashesPerSec;
}


Value getinfo(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getinfo\n"
            "Returns an object containing various state info.");

    Object obj;
    obj.push_back(Pair("version",       (int)VERSION));
//    obj.push_back(Pair("balance",       ValueFromAmount(pwalletMain->GetBalance())));
    obj.push_back(Pair("blocks",        (int)nBestHeight));
    obj.push_back(Pair("connections",   (int)vNodes.size()));
    obj.push_back(Pair("proxy",         (fUseProxy ? addrProxy.ToStringIPPort() : string())));
//    obj.push_back(Pair("generate",      (bool)fGenerateBitcoins));
    obj.push_back(Pair("genproclimit",  (int)(fLimitProcessors ? nLimitProcessors : -1)));
    obj.push_back(Pair("difficulty",    (double)GetDifficulty()));
    obj.push_back(Pair("hashespersec",  gethashespersec(params, false)));
    obj.push_back(Pair("testnet",       fTestNet));
//    obj.push_back(Pair("keypoololdest", (boost::int64_t)pwalletMain->GetOldestKeyPoolTime()));
//    obj.push_back(Pair("keypoolsize",   pwalletMain->GetKeyPoolSize()));
//    obj.push_back(Pair("paytxfee",      ValueFromAmount(nTransactionFee)));
//    if (pwalletMain->IsCrypted())
//        obj.push_back(Pair("unlocked_until", (boost::int64_t)nWalletUnlockTime));
    obj.push_back(Pair("errors",        GetWarnings("statusbar")));
    return obj;
}

/*
Value getnewaddress(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw runtime_error(
            "getnewaddress [account]\n"
            "Returns a new bitcoin address for receiving payments.  "
            "If [account] is specified (recommended), it is added to the address book "
            "so payments received with the address will be credited to [account].");

    // Parse the account first so we don't generate a key if there's an error
    string strAccount;
    if (params.size() > 0)
        strAccount = AccountFromValue(params[0]);

    if (!pwalletMain->IsLocked())
        pwalletMain->TopUpKeyPool();

    // Generate a new key that is added to wallet
    std::vector<unsigned char> newKey;
    if (!pwalletMain->GetKeyFromPool(newKey, false))
        throw JSONRPCError(-12, "Error: Keypool ran out, please call keypoolrefill first");
    CBitcoinAddress address(newKey);

    pwalletMain->SetAddressBookName(address, strAccount);

    return address.ToString();
}


CBitcoinAddress GetAccountAddress(string strAccount, bool bForceNew=false)
{
    CWalletDB walletdb(pwalletMain->strWalletFile);

    CAccount account;
    walletdb.ReadAccount(strAccount, account);

    bool bKeyUsed = false;

    // Check if the current key has been used
    if (!account.vchPubKey.empty())
    {
        CScript scriptPubKey;
        scriptPubKey.SetBitcoinAddress(account.vchPubKey);
        for (map<uint256, CWalletTx>::iterator it = pwalletMain->mapWallet.begin();
             it != pwalletMain->mapWallet.end() && !account.vchPubKey.empty();
             ++it)
        {
            const CWalletTx& wtx = (*it).second;
            BOOST_FOREACH(const CTxOut& txout, wtx.vout)
                if (txout.scriptPubKey == scriptPubKey)
                    bKeyUsed = true;
        }
    }

    // Generate a new key
    if (account.vchPubKey.empty() || bForceNew || bKeyUsed)
    {
        if (!pwalletMain->GetKeyFromPool(account.vchPubKey, false))
            throw JSONRPCError(-12, "Error: Keypool ran out, please call keypoolrefill first");

        pwalletMain->SetAddressBookName(CBitcoinAddress(account.vchPubKey), strAccount);
        walletdb.WriteAccount(strAccount, account);
    }

    return CBitcoinAddress(account.vchPubKey);
}

Value getaccountaddress(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "getaccountaddress <account>\n"
            "Returns the current bitcoin address for receiving payments to this account.");

    // Parse the account first so we don't generate a key if there's an error
    string strAccount = AccountFromValue(params[0]);

    Value ret;

    ret = GetAccountAddress(strAccount).ToString();

    return ret;
}



Value setaccount(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "setaccount <bitcoinaddress> <account>\n"
            "Sets the account associated with the given address.");

    CBitcoinAddress address(params[0].get_str());
    if (!address.IsValid())
        throw JSONRPCError(-5, "Invalid bitcoin address");


    string strAccount;
    if (params.size() > 1)
        strAccount = AccountFromValue(params[1]);

    // Detect when changing the account of an address that is the 'unused current key' of another account:
    if (pwalletMain->mapAddressBook.count(address))
    {
        string strOldAccount = pwalletMain->mapAddressBook[address];
        if (address == GetAccountAddress(strOldAccount))
            GetAccountAddress(strOldAccount, true);
    }

    pwalletMain->SetAddressBookName(address, strAccount);

    return Value::null;
}


Value getaccount(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "getaccount <bitcoinaddress>\n"
            "Returns the account associated with the given address.");

    CBitcoinAddress address(params[0].get_str());
    if (!address.IsValid())
        throw JSONRPCError(-5, "Invalid bitcoin address");

    string strAccount;
    map<CBitcoinAddress, string>::iterator mi = pwalletMain->mapAddressBook.find(address);
    if (mi != pwalletMain->mapAddressBook.end() && !(*mi).second.empty())
        strAccount = (*mi).second;
    return strAccount;
}


Value getaddressesbyaccount(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "getaddressesbyaccount <account>\n"
            "Returns the list of addresses for the given account.");

    string strAccount = AccountFromValue(params[0]);

    // Find all addresses that have the given account
    Array ret;
    BOOST_FOREACH(const PAIRTYPE(CBitcoinAddress, string)& item, pwalletMain->mapAddressBook)
    {
        const CBitcoinAddress& address = item.first;
        const string& strName = item.second;
        if (strName == strAccount)
            ret.push_back(address.ToString());
    }
    return ret;
}

Value settxfee(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 1)
        throw runtime_error(
            "settxfee <amount>\n"
            "<amount> is a real and is rounded to the nearest 0.00000001");

    // Amount
    int64 nAmount = 0;
    if (params[0].get_real() != 0.0)
        nAmount = AmountFromValue(params[0]);        // rejects 0.0 amounts

    nTransactionFee = nAmount;
    return true;
}

Value sendtoaddress(const Array& params, bool fHelp)
{
    if (pwalletMain->IsCrypted() && (fHelp || params.size() < 2 || params.size() > 4))
        throw runtime_error(
            "sendtoaddress <bitcoinaddress> <amount> [comment] [comment-to]\n"
            "<amount> is a real and is rounded to the nearest 0.00000001\n"
            "requires wallet passphrase to be set with walletpassphrase first");
    if (!pwalletMain->IsCrypted() && (fHelp || params.size() < 2 || params.size() > 4))
        throw runtime_error(
            "sendtoaddress <bitcoinaddress> <amount> [comment] [comment-to]\n"
            "<amount> is a real and is rounded to the nearest 0.00000001");

    CBitcoinAddress address(params[0].get_str());
    if (!address.IsValid())
        throw JSONRPCError(-5, "Invalid bitcoin address");

    // Amount
    int64 nAmount = AmountFromValue(params[1]);

    // Wallet comments
    CWalletTx wtx;
    if (params.size() > 2 && params[2].type() != null_type && !params[2].get_str().empty())
        wtx.mapValue["comment"] = params[2].get_str();
    if (params.size() > 3 && params[3].type() != null_type && !params[3].get_str().empty())
        wtx.mapValue["to"]      = params[3].get_str();

    if (pwalletMain->IsLocked())
        throw JSONRPCError(-13, "Error: Please enter the wallet passphrase with walletpassphrase first.");

    string strError = pwalletMain->SendMoneyToBitcoinAddress(address, nAmount, wtx);
    if (strError != "")
        throw JSONRPCError(-4, strError);

    return wtx.GetHash().GetHex();
}


Value getreceivedbyaddress(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "getreceivedbyaddress <bitcoinaddress> [minconf=1]\n"
            "Returns the total amount received by <bitcoinaddress> in transactions with at least [minconf] confirmations.");

    // Bitcoin address
    CBitcoinAddress address = CBitcoinAddress(params[0].get_str());
    CScript scriptPubKey;
    if (!address.IsValid())
        throw JSONRPCError(-5, "Invalid bitcoin address");
    scriptPubKey.SetBitcoinAddress(address);
    if (!IsMine(*pwalletMain,scriptPubKey))
        return (double)0.0;

    // Minimum confirmations
    int nMinDepth = 1;
    if (params.size() > 1)
        nMinDepth = params[1].get_int();

    // Tally
    int64 nAmount = 0;
    for (map<uint256, CWalletTx>::iterator it = pwalletMain->mapWallet.begin(); it != pwalletMain->mapWallet.end(); ++it)
    {
        const CWalletTx& wtx = (*it).second;
        if (wtx.IsCoinBase() || !wtx.IsFinal())
            continue;

        BOOST_FOREACH(const CTxOut& txout, wtx.vout)
            if (txout.scriptPubKey == scriptPubKey)
                if (wtx.GetDepthInMainChain() >= nMinDepth)
                    nAmount += txout.nValue;
    }

    return  ValueFromAmount(nAmount);
}


void GetAccountAddresses(string strAccount, set<CBitcoinAddress>& setAddress)
{
    BOOST_FOREACH(const PAIRTYPE(CBitcoinAddress, string)& item, pwalletMain->mapAddressBook)
    {
        const CBitcoinAddress& address = item.first;
        const string& strName = item.second;
        if (strName == strAccount)
            setAddress.insert(address);
    }
}


Value getreceivedbyaccount(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "getreceivedbyaccount <account> [minconf=1]\n"
            "Returns the total amount received by addresses with <account> in transactions with at least [minconf] confirmations.");

    // Minimum confirmations
    int nMinDepth = 1;
    if (params.size() > 1)
        nMinDepth = params[1].get_int();

    // Get the set of pub keys that have the label
    string strAccount = AccountFromValue(params[0]);
    set<CBitcoinAddress> setAddress;
    GetAccountAddresses(strAccount, setAddress);

    // Tally
    int64 nAmount = 0;
    for (map<uint256, CWalletTx>::iterator it = pwalletMain->mapWallet.begin(); it != pwalletMain->mapWallet.end(); ++it)
    {
        const CWalletTx& wtx = (*it).second;
        if (wtx.IsCoinBase() || !wtx.IsFinal())
            continue;

        BOOST_FOREACH(const CTxOut& txout, wtx.vout)
        {
            CBitcoinAddress address;
            if (ExtractAddress(txout.scriptPubKey, pwalletMain, address) && setAddress.count(address))
                if (wtx.GetDepthInMainChain() >= nMinDepth)
                    nAmount += txout.nValue;
        }
    }

    return (double)nAmount / (double)COIN;
}


int64 GetAccountBalance(CWalletDB& walletdb, const string& strAccount, int nMinDepth)
{
    int64 nBalance = 0;

    // Tally wallet transactions
    for (map<uint256, CWalletTx>::iterator it = pwalletMain->mapWallet.begin(); it != pwalletMain->mapWallet.end(); ++it)
    {
        const CWalletTx& wtx = (*it).second;
        if (!wtx.IsFinal())
            continue;

        int64 nGenerated, nReceived, nSent, nFee;
        wtx.GetAccountAmounts(strAccount, nGenerated, nReceived, nSent, nFee);

        if (nReceived != 0 && wtx.GetDepthInMainChain() >= nMinDepth)
            nBalance += nReceived;
        nBalance += nGenerated - nSent - nFee;
    }

    // Tally internal accounting entries
    nBalance += walletdb.GetAccountCreditDebit(strAccount);

    return nBalance;
}

int64 GetAccountBalance(const string& strAccount, int nMinDepth)
{
    CWalletDB walletdb(pwalletMain->strWalletFile);
    return GetAccountBalance(walletdb, strAccount, nMinDepth);
}


Value getbalance(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 2)
        throw runtime_error(
            "getbalance [account] [minconf=1]\n"
            "If [account] is not specified, returns the server's total available balance.\n"
            "If [account] is specified, returns the balance in the account.");

    if (params.size() == 0)
        return  ValueFromAmount(pwalletMain->GetBalance());

    int nMinDepth = 1;
    if (params.size() > 1)
        nMinDepth = params[1].get_int();

    if (params[0].get_str() == "*") {
        // Calculate total balance a different way from GetBalance()
        // (GetBalance() sums up all unspent TxOuts)
        // getbalance and getbalance '*' should always return the same number.
        int64 nBalance = 0;
        for (map<uint256, CWalletTx>::iterator it = pwalletMain->mapWallet.begin(); it != pwalletMain->mapWallet.end(); ++it)
        {
            const CWalletTx& wtx = (*it).second;
            if (!wtx.IsFinal())
                continue;

            int64 allGeneratedImmature, allGeneratedMature, allFee;
            allGeneratedImmature = allGeneratedMature = allFee = 0;
            string strSentAccount;
            list<pair<CBitcoinAddress, int64> > listReceived;
            list<pair<CBitcoinAddress, int64> > listSent;
            wtx.GetAmounts(allGeneratedImmature, allGeneratedMature, listReceived, listSent, allFee, strSentAccount);
            if (wtx.GetDepthInMainChain() >= nMinDepth)
                BOOST_FOREACH(const PAIRTYPE(CBitcoinAddress,int64)& r, listReceived)
                    nBalance += r.second;
            BOOST_FOREACH(const PAIRTYPE(CBitcoinAddress,int64)& r, listSent)
                nBalance -= r.second;
            nBalance -= allFee;
            nBalance += allGeneratedMature;
        }
        return  ValueFromAmount(nBalance);
    }

    string strAccount = AccountFromValue(params[0]);

    int64 nBalance = GetAccountBalance(strAccount, nMinDepth);

    return ValueFromAmount(nBalance);
}


Value movecmd(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 3 || params.size() > 5)
        throw runtime_error(
            "move <fromaccount> <toaccount> <amount> [minconf=1] [comment]\n"
            "Move from one account in your wallet to another.");

    string strFrom = AccountFromValue(params[0]);
    string strTo = AccountFromValue(params[1]);
    int64 nAmount = AmountFromValue(params[2]);
    if (params.size() > 3)
        // unused parameter, used to be nMinDepth, keep type-checking it though
        (void)params[3].get_int();
    string strComment;
    if (params.size() > 4)
        strComment = params[4].get_str();

    CWalletDB walletdb(pwalletMain->strWalletFile);
    walletdb.TxnBegin();

    int64 nNow = GetAdjustedTime();

    // Debit
    CAccountingEntry debit;
    debit.strAccount = strFrom;
    debit.nCreditDebit = -nAmount;
    debit.nTime = nNow;
    debit.strOtherAccount = strTo;
    debit.strComment = strComment;
    walletdb.WriteAccountingEntry(debit);

    // Credit
    CAccountingEntry credit;
    credit.strAccount = strTo;
    credit.nCreditDebit = nAmount;
    credit.nTime = nNow;
    credit.strOtherAccount = strFrom;
    credit.strComment = strComment;
    walletdb.WriteAccountingEntry(credit);

    walletdb.TxnCommit();

    return true;
}


Value sendfrom(const Array& params, bool fHelp)
{
    if (pwalletMain->IsCrypted() && (fHelp || params.size() < 3 || params.size() > 6))
        throw runtime_error(
            "sendfrom <fromaccount> <tobitcoinaddress> <amount> [minconf=1] [comment] [comment-to]\n"
            "<amount> is a real and is rounded to the nearest 0.00000001\n"
            "requires wallet passphrase to be set with walletpassphrase first");
    if (!pwalletMain->IsCrypted() && (fHelp || params.size() < 3 || params.size() > 6))
        throw runtime_error(
            "sendfrom <fromaccount> <tobitcoinaddress> <amount> [minconf=1] [comment] [comment-to]\n"
            "<amount> is a real and is rounded to the nearest 0.00000001");

    string strAccount = AccountFromValue(params[0]);
    CBitcoinAddress address(params[1].get_str());
    if (!address.IsValid())
        throw JSONRPCError(-5, "Invalid bitcoin address");
    int64 nAmount = AmountFromValue(params[2]);
    int nMinDepth = 1;
    if (params.size() > 3)
        nMinDepth = params[3].get_int();

    CWalletTx wtx;
    wtx.strFromAccount = strAccount;
    if (params.size() > 4 && params[4].type() != null_type && !params[4].get_str().empty())
        wtx.mapValue["comment"] = params[4].get_str();
    if (params.size() > 5 && params[5].type() != null_type && !params[5].get_str().empty())
        wtx.mapValue["to"]      = params[5].get_str();

    if (pwalletMain->IsLocked())
        throw JSONRPCError(-13, "Error: Please enter the wallet passphrase with walletpassphrase first.");

    // Check funds
    int64 nBalance = GetAccountBalance(strAccount, nMinDepth);
    if (nAmount > nBalance)
        throw JSONRPCError(-6, "Account has insufficient funds");

    // Send
    string strError = pwalletMain->SendMoneyToBitcoinAddress(address, nAmount, wtx);
    if (strError != "")
        throw JSONRPCError(-4, strError);

    return wtx.GetHash().GetHex();
}


Value sendmany(const Array& params, bool fHelp)
{
    if (pwalletMain->IsCrypted() && (fHelp || params.size() < 2 || params.size() > 4))
        throw runtime_error(
            "sendmany <fromaccount> {address:amount,...} [minconf=1] [comment]\n"
            "amounts are double-precision floating point numbers\n"
            "requires wallet passphrase to be set with walletpassphrase first");
    if (!pwalletMain->IsCrypted() && (fHelp || params.size() < 2 || params.size() > 4))
        throw runtime_error(
            "sendmany <fromaccount> {address:amount,...} [minconf=1] [comment]\n"
            "amounts are double-precision floating point numbers");

    string strAccount = AccountFromValue(params[0]);
    Object sendTo = params[1].get_obj();
    int nMinDepth = 1;
    if (params.size() > 2)
        nMinDepth = params[2].get_int();

    CWalletTx wtx;
    wtx.strFromAccount = strAccount;
    if (params.size() > 3 && params[3].type() != null_type && !params[3].get_str().empty())
        wtx.mapValue["comment"] = params[3].get_str();

    set<CBitcoinAddress> setAddress;
    vector<pair<CScript, int64> > vecSend;

    int64 totalAmount = 0;
    BOOST_FOREACH(const Pair& s, sendTo)
    {
        CBitcoinAddress address(s.name_);
        if (!address.IsValid())
            throw JSONRPCError(-5, string("Invalid bitcoin address:")+s.name_);

        if (setAddress.count(address))
            throw JSONRPCError(-8, string("Invalid parameter, duplicated address: ")+s.name_);
        setAddress.insert(address);

        CScript scriptPubKey;
        scriptPubKey.SetBitcoinAddress(address);
        int64 nAmount = AmountFromValue(s.value_); 
        totalAmount += nAmount;

        vecSend.push_back(make_pair(scriptPubKey, nAmount));
    }

    if (pwalletMain->IsLocked())
        throw JSONRPCError(-13, "Error: Please enter the wallet passphrase with walletpassphrase first.");

    // Check funds
    int64 nBalance = GetAccountBalance(strAccount, nMinDepth);
    if (totalAmount > nBalance)
        throw JSONRPCError(-6, "Account has insufficient funds");

    // Send
    CReserveKey keyChange(pwalletMain);
    int64 nFeeRequired = 0;
    bool fCreated = pwalletMain->CreateTransaction(vecSend, wtx, keyChange, nFeeRequired);
    if (!fCreated)
    {
        if (totalAmount + nFeeRequired > pwalletMain->GetBalance())
            throw JSONRPCError(-6, "Insufficient funds");
        throw JSONRPCError(-4, "Transaction creation failed");
    }
    if (!pwalletMain->CommitTransaction(wtx, keyChange))
        throw JSONRPCError(-4, "Transaction commit failed");

    return wtx.GetHash().GetHex();
}


struct tallyitem
{
    int64 nAmount;
    int nConf;
    tallyitem()
    {
        nAmount = 0;
        nConf = INT_MAX;
    }
};

Value ListReceived(const Array& params, bool fByAccounts)
{
    // Minimum confirmations
    int nMinDepth = 1;
    if (params.size() > 0)
        nMinDepth = params[0].get_int();

    // Whether to include empty accounts
    bool fIncludeEmpty = false;
    if (params.size() > 1)
        fIncludeEmpty = params[1].get_bool();

    // Tally
    map<CBitcoinAddress, tallyitem> mapTally;
    for (map<uint256, CWalletTx>::iterator it = pwalletMain->mapWallet.begin(); it != pwalletMain->mapWallet.end(); ++it)
    {
        const CWalletTx& wtx = (*it).second;
        if (wtx.IsCoinBase() || !wtx.IsFinal())
            continue;

        int nDepth = wtx.GetDepthInMainChain();
        if (nDepth < nMinDepth)
            continue;

        BOOST_FOREACH(const CTxOut& txout, wtx.vout)
        {
            CBitcoinAddress address;
            if (!ExtractAddress(txout.scriptPubKey, pwalletMain, address) || !address.IsValid())
                continue;

            tallyitem& item = mapTally[address];
            item.nAmount += txout.nValue;
            item.nConf = min(item.nConf, nDepth);
        }
    }

    // Reply
    Array ret;
    map<string, tallyitem> mapAccountTally;
    BOOST_FOREACH(const PAIRTYPE(CBitcoinAddress, string)& item, pwalletMain->mapAddressBook)
    {
        const CBitcoinAddress& address = item.first;
        const string& strAccount = item.second;
        map<CBitcoinAddress, tallyitem>::iterator it = mapTally.find(address);
        if (it == mapTally.end() && !fIncludeEmpty)
            continue;

        int64 nAmount = 0;
        int nConf = INT_MAX;
        if (it != mapTally.end())
        {
            nAmount = (*it).second.nAmount;
            nConf = (*it).second.nConf;
        }

        if (fByAccounts)
        {
            tallyitem& item = mapAccountTally[strAccount];
            item.nAmount += nAmount;
            item.nConf = min(item.nConf, nConf);
        }
        else
        {
            Object obj;
            obj.push_back(Pair("address",       address.ToString()));
            obj.push_back(Pair("account",       strAccount));
            obj.push_back(Pair("label",         strAccount)); // deprecated
            obj.push_back(Pair("amount",        ValueFromAmount(nAmount)));
            obj.push_back(Pair("confirmations", (nConf == INT_MAX ? 0 : nConf)));
            ret.push_back(obj);
        }
    }

    if (fByAccounts)
    {
        for (map<string, tallyitem>::iterator it = mapAccountTally.begin(); it != mapAccountTally.end(); ++it)
        {
            int64 nAmount = (*it).second.nAmount;
            int nConf = (*it).second.nConf;
            Object obj;
            obj.push_back(Pair("account",       (*it).first));
            obj.push_back(Pair("label",         (*it).first)); // deprecated
            obj.push_back(Pair("amount",        ValueFromAmount(nAmount)));
            obj.push_back(Pair("confirmations", (nConf == INT_MAX ? 0 : nConf)));
            ret.push_back(obj);
        }
    }

    return ret;
}

Value listreceivedbyaddress(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 2)
        throw runtime_error(
            "listreceivedbyaddress [minconf=1] [includeempty=false]\n"
            "[minconf] is the minimum number of confirmations before payments are included.\n"
            "[includeempty] whether to include addresses that haven't received any payments.\n"
            "Returns an array of objects containing:\n"
            "  \"address\" : receiving address\n"
            "  \"account\" : the account of the receiving address\n"
            "  \"amount\" : total amount received by the address\n"
            "  \"confirmations\" : number of confirmations of the most recent transaction included");

    return ListReceived(params, false);
}

Value listreceivedbyaccount(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 2)
        throw runtime_error(
            "listreceivedbyaccount [minconf=1] [includeempty=false]\n"
            "[minconf] is the minimum number of confirmations before payments are included.\n"
            "[includeempty] whether to include accounts that haven't received any payments.\n"
            "Returns an array of objects containing:\n"
            "  \"account\" : the account of the receiving addresses\n"
            "  \"amount\" : total amount received by addresses with this account\n"
            "  \"confirmations\" : number of confirmations of the most recent transaction included");

    return ListReceived(params, true);
}

void ListTransactions(const CWalletTx& wtx, const string& strAccount, int nMinDepth, bool fLong, Array& ret)
{
    int64 nGeneratedImmature, nGeneratedMature, nFee;
    string strSentAccount;
    list<pair<CBitcoinAddress, int64> > listReceived;
    list<pair<CBitcoinAddress, int64> > listSent;
    wtx.GetAmounts(nGeneratedImmature, nGeneratedMature, listReceived, listSent, nFee, strSentAccount);

    bool fAllAccounts = (strAccount == string("*"));

    // Generated blocks assigned to account ""
    if ((nGeneratedMature+nGeneratedImmature) != 0 && (fAllAccounts || strAccount == ""))
    {
        Object entry;
        entry.push_back(Pair("account", string("")));
        if (nGeneratedImmature)
        {
            entry.push_back(Pair("category", wtx.GetDepthInMainChain() ? "immature" : "orphan"));
            entry.push_back(Pair("amount", ValueFromAmount(nGeneratedImmature)));
        }
        else
        {
            entry.push_back(Pair("category", "generate"));
            entry.push_back(Pair("amount", ValueFromAmount(nGeneratedMature)));
        }
        if (fLong)
            WalletTxToJSON(wtx, entry);
        ret.push_back(entry);
    }

    // Sent
    if ((!listSent.empty() || nFee != 0) && (fAllAccounts || strAccount == strSentAccount))
    {
        BOOST_FOREACH(const PAIRTYPE(CBitcoinAddress, int64)& s, listSent)
        {
            Object entry;
            entry.push_back(Pair("account", strSentAccount));
            entry.push_back(Pair("address", s.first.ToString()));
            entry.push_back(Pair("category", "send"));
            entry.push_back(Pair("amount", ValueFromAmount(-s.second)));
            entry.push_back(Pair("fee", ValueFromAmount(-nFee)));
            if (fLong)
                WalletTxToJSON(wtx, entry);
            ret.push_back(entry);
        }
    }

    // Received
    if (listReceived.size() > 0 && wtx.GetDepthInMainChain() >= nMinDepth)
        BOOST_FOREACH(const PAIRTYPE(CBitcoinAddress, int64)& r, listReceived)
        {
            string account;
            if (pwalletMain->mapAddressBook.count(r.first))
                account = pwalletMain->mapAddressBook[r.first];
            if (fAllAccounts || (account == strAccount))
            {
                Object entry;
                entry.push_back(Pair("account", account));
                entry.push_back(Pair("address", r.first.ToString()));
                entry.push_back(Pair("category", "receive"));
                entry.push_back(Pair("amount", ValueFromAmount(r.second)));
                if (fLong)
                    WalletTxToJSON(wtx, entry);
                ret.push_back(entry);
            }
        }
}

void AcentryToJSON(const CAccountingEntry& acentry, const string& strAccount, Array& ret)
{
    bool fAllAccounts = (strAccount == string("*"));

    if (fAllAccounts || acentry.strAccount == strAccount)
    {
        Object entry;
        entry.push_back(Pair("account", acentry.strAccount));
        entry.push_back(Pair("category", "move"));
        entry.push_back(Pair("time", (boost::int64_t)acentry.nTime));
        entry.push_back(Pair("amount", ValueFromAmount(acentry.nCreditDebit)));
        entry.push_back(Pair("otheraccount", acentry.strOtherAccount));
        entry.push_back(Pair("comment", acentry.strComment));
        ret.push_back(entry);
    }
}

Value listtransactions(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 3)
        throw runtime_error(
            "listtransactions [account] [count=10] [from=0]\n"
            "Returns up to [count] most recent transactions skipping the first [from] transactions for account [account].");

    string strAccount = "*";
    if (params.size() > 0)
        strAccount = params[0].get_str();
    int nCount = 10;
    if (params.size() > 1)
        nCount = params[1].get_int();
    int nFrom = 0;
    if (params.size() > 2)
        nFrom = params[2].get_int();

    Array ret;
    CWalletDB walletdb(pwalletMain->strWalletFile);

    // Firs: get all CWalletTx and CAccountingEntry into a sorted-by-time multimap:
    typedef pair<CWalletTx*, CAccountingEntry*> TxPair;
    typedef multimap<int64, TxPair > TxItems;
    TxItems txByTime;

    for (map<uint256, CWalletTx>::iterator it = pwalletMain->mapWallet.begin(); it != pwalletMain->mapWallet.end(); ++it)
    {
        CWalletTx* wtx = &((*it).second);
        txByTime.insert(make_pair(wtx->GetTxTime(), TxPair(wtx, (CAccountingEntry*)0)));
    }
    list<CAccountingEntry> acentries;
    walletdb.ListAccountCreditDebit(strAccount, acentries);
    BOOST_FOREACH(CAccountingEntry& entry, acentries)
    {
        txByTime.insert(make_pair(entry.nTime, TxPair((CWalletTx*)0, &entry)));
    }

    // Now: iterate backwards until we have nCount items to return:
    TxItems::reverse_iterator it = txByTime.rbegin();
    if (txByTime.size() > nFrom) std::advance(it, nFrom);
    for (; it != txByTime.rend(); ++it)
    {
        CWalletTx *const pwtx = (*it).second.first;
        if (pwtx != 0)
            ListTransactions(*pwtx, strAccount, 0, true, ret);
        CAccountingEntry *const pacentry = (*it).second.second;
        if (pacentry != 0)
            AcentryToJSON(*pacentry, strAccount, ret);

        if (ret.size() >= nCount) break;
    }
    // ret is now newest to oldest
    
    // Make sure we return only last nCount items (sends-to-self might give us an extra):
    if (ret.size() > nCount)
    {
        Array::iterator last = ret.begin();
        std::advance(last, nCount);
        ret.erase(last, ret.end());
    }
    std::reverse(ret.begin(), ret.end()); // oldest to newest

    return ret;
}

Value listaccounts(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw runtime_error(
            "listaccounts [minconf=1]\n"
            "Returns Object that has account names as keys, account balances as values.");

    int nMinDepth = 1;
    if (params.size() > 0)
        nMinDepth = params[0].get_int();

    map<string, int64> mapAccountBalances;
    BOOST_FOREACH(const PAIRTYPE(CBitcoinAddress, string)& entry, pwalletMain->mapAddressBook) {
        if (pwalletMain->HaveKey(entry.first)) // This address belongs to me
            mapAccountBalances[entry.second] = 0;
    }

    for (map<uint256, CWalletTx>::iterator it = pwalletMain->mapWallet.begin(); it != pwalletMain->mapWallet.end(); ++it)
    {
        const CWalletTx& wtx = (*it).second;
        int64 nGeneratedImmature, nGeneratedMature, nFee;
        string strSentAccount;
        list<pair<CBitcoinAddress, int64> > listReceived;
        list<pair<CBitcoinAddress, int64> > listSent;
        wtx.GetAmounts(nGeneratedImmature, nGeneratedMature, listReceived, listSent, nFee, strSentAccount);
        mapAccountBalances[strSentAccount] -= nFee;
        BOOST_FOREACH(const PAIRTYPE(CBitcoinAddress, int64)& s, listSent)
            mapAccountBalances[strSentAccount] -= s.second;
        if (wtx.GetDepthInMainChain() >= nMinDepth)
        {
            mapAccountBalances[""] += nGeneratedMature;
            BOOST_FOREACH(const PAIRTYPE(CBitcoinAddress, int64)& r, listReceived)
                if (pwalletMain->mapAddressBook.count(r.first))
                    mapAccountBalances[pwalletMain->mapAddressBook[r.first]] += r.second;
                else
                    mapAccountBalances[""] += r.second;
        }
    }

    list<CAccountingEntry> acentries;
    CWalletDB(pwalletMain->strWalletFile).ListAccountCreditDebit("*", acentries);
    BOOST_FOREACH(const CAccountingEntry& entry, acentries)
        mapAccountBalances[entry.strAccount] += entry.nCreditDebit;

    Object ret;
    BOOST_FOREACH(const PAIRTYPE(string, int64)& accountBalance, mapAccountBalances) {
        ret.push_back(Pair(accountBalance.first, ValueFromAmount(accountBalance.second)));
    }
    return ret;
}

Value gettransaction(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "gettransaction <txid>\n"
            "Get detailed information about <txid>");

    uint256 hash;
    hash.SetHex(params[0].get_str());

    Object entry;

    if (!pwalletMain->mapWallet.count(hash))
        throw JSONRPCError(-5, "Invalid or non-wallet transaction id");
    const CWalletTx& wtx = pwalletMain->mapWallet[hash];

    int64 nCredit = wtx.GetCredit();
    int64 nDebit = wtx.GetDebit();
    int64 nNet = nCredit - nDebit;
    int64 nFee = (wtx.IsFromMe() ? wtx.GetValueOut() - nDebit : 0);

    entry.push_back(Pair("amount", ValueFromAmount(nNet - nFee)));
    if (wtx.IsFromMe())
        entry.push_back(Pair("fee", ValueFromAmount(nFee)));

    WalletTxToJSON(pwalletMain->mapWallet[hash], entry);

    Array details;
    ListTransactions(pwalletMain->mapWallet[hash], "*", 0, false, details);
    entry.push_back(Pair("details", details));

    return entry;
}


Value backupwallet(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "backupwallet <destination>\n"
            "Safely copies wallet.dat to destination, which can be a directory or a path with filename.");

    string strDest = params[0].get_str();
    BackupWallet(*pwalletMain, strDest);

    return Value::null;
}


Value keypoolrefill(const Array& params, bool fHelp)
{
    if (pwalletMain->IsCrypted() && (fHelp || params.size() > 0))
        throw runtime_error(
            "keypoolrefill\n"
            "Fills the keypool, requires wallet passphrase to be set.");
    if (!pwalletMain->IsCrypted() && (fHelp || params.size() > 0))
        throw runtime_error(
            "keypoolrefill\n"
            "Fills the keypool.");

    if (pwalletMain->IsLocked())
        throw JSONRPCError(-13, "Error: Please enter the wallet passphrase with walletpassphrase first.");

    pwalletMain->TopUpKeyPool();

    if (pwalletMain->GetKeyPoolSize() < GetArg("-keypool", 100))
        throw JSONRPCError(-4, "Error refreshing keypool.");

    return Value::null;
}


void ThreadTopUpKeyPool(void* parg)
{
    pwalletMain->TopUpKeyPool();
}

void ThreadCleanWalletPassphrase(void* parg)
{
    int64 nMyWakeTime = GetTime() + *((int*)parg);

    if (nWalletUnlockTime == 0)
    {
        CRITICAL_BLOCK(cs_nWalletUnlockTime)
        {
            nWalletUnlockTime = nMyWakeTime;
        }

        while (GetTime() < nWalletUnlockTime)
            Sleep(GetTime() - nWalletUnlockTime);

        CRITICAL_BLOCK(cs_nWalletUnlockTime)
        {
            nWalletUnlockTime = 0;
        }
    }
    else
    {
        CRITICAL_BLOCK(cs_nWalletUnlockTime)
        {
            if (nWalletUnlockTime < nMyWakeTime)
                nWalletUnlockTime = nMyWakeTime;
        }
        free(parg);
        return;
    }

    pwalletMain->Lock();

    delete (int*)parg;
}

Value walletpassphrase(const Array& params, bool fHelp)
{
    if (pwalletMain->IsCrypted() && (fHelp || params.size() != 2))
        throw runtime_error(
            "walletpassphrase <passphrase> <timeout>\n"
            "Stores the wallet decryption key in memory for <timeout> seconds.");
    if (fHelp)
        return true;
    if (!pwalletMain->IsCrypted())
        throw JSONRPCError(-15, "Error: running with an unencrypted wallet, but walletpassphrase was called.");

    if (!pwalletMain->IsLocked())
        throw JSONRPCError(-17, "Error: Wallet is already unlocked.");

    // Note that the walletpassphrase is stored in params[0] which is not mlock()ed
    string strWalletPass;
    strWalletPass.reserve(100);
    mlock(&strWalletPass[0], strWalletPass.capacity());
    strWalletPass = params[0].get_str();

    if (strWalletPass.length() > 0)
    {
        if (!pwalletMain->Unlock(strWalletPass))
        {
            fill(strWalletPass.begin(), strWalletPass.end(), '\0');
            munlock(&strWalletPass[0], strWalletPass.capacity());
            throw JSONRPCError(-14, "Error: The wallet passphrase entered was incorrect.");
        }
        fill(strWalletPass.begin(), strWalletPass.end(), '\0');
        munlock(&strWalletPass[0], strWalletPass.capacity());
    }
    else
        throw runtime_error(
            "walletpassphrase <passphrase> <timeout>\n"
            "Stores the wallet decryption key in memory for <timeout> seconds.");

    CreateThread(ThreadTopUpKeyPool, NULL);
    int* pnSleepTime = new int(params[1].get_int());
    CreateThread(ThreadCleanWalletPassphrase, pnSleepTime);

    return Value::null;
}


Value walletpassphrasechange(const Array& params, bool fHelp)
{
    if (pwalletMain->IsCrypted() && (fHelp || params.size() != 2))
        throw runtime_error(
            "walletpassphrasechange <oldpassphrase> <newpassphrase>\n"
            "Changes the wallet passphrase from <oldpassphrase> to <newpassphrase>.");
    if (fHelp)
        return true;
    if (!pwalletMain->IsCrypted())
        throw JSONRPCError(-15, "Error: running with an unencrypted wallet, but walletpassphrasechange was called.");

    string strOldWalletPass;
    strOldWalletPass.reserve(100);
    mlock(&strOldWalletPass[0], strOldWalletPass.capacity());
    strOldWalletPass = params[0].get_str();

    string strNewWalletPass;
    strNewWalletPass.reserve(100);
    mlock(&strNewWalletPass[0], strNewWalletPass.capacity());
    strNewWalletPass = params[1].get_str();

    if (strOldWalletPass.length() < 1 || strNewWalletPass.length() < 1)
        throw runtime_error(
            "walletpassphrasechange <oldpassphrase> <newpassphrase>\n"
            "Changes the wallet passphrase from <oldpassphrase> to <newpassphrase>.");

    if (!pwalletMain->ChangeWalletPassphrase(strOldWalletPass, strNewWalletPass))
    {
        fill(strOldWalletPass.begin(), strOldWalletPass.end(), '\0');
        fill(strNewWalletPass.begin(), strNewWalletPass.end(), '\0');
        munlock(&strOldWalletPass[0], strOldWalletPass.capacity());
        munlock(&strNewWalletPass[0], strNewWalletPass.capacity());
        throw JSONRPCError(-14, "Error: The wallet passphrase entered was incorrect.");
    }
    fill(strNewWalletPass.begin(), strNewWalletPass.end(), '\0');
    fill(strOldWalletPass.begin(), strOldWalletPass.end(), '\0');
    munlock(&strOldWalletPass[0], strOldWalletPass.capacity());
    munlock(&strNewWalletPass[0], strNewWalletPass.capacity());

    return Value::null;
}


Value walletlock(const Array& params, bool fHelp)
{
    if (pwalletMain->IsCrypted() && (fHelp || params.size() != 0))
        throw runtime_error(
            "walletlock\n"
            "Removes the wallet encryption key from memory, locking the wallet.\n"
            "After calling this method, you will need to call walletpassphrase again\n"
            "before being able to call any methods which require the wallet to be unlocked.");
    if (fHelp)
        return true;
    if (!pwalletMain->IsCrypted())
        throw JSONRPCError(-15, "Error: running with an unencrypted wallet, but walletlock was called.");

    pwalletMain->Lock();
    CRITICAL_BLOCK(cs_nWalletUnlockTime)
    {
        nWalletUnlockTime = 0;
    }

    return Value::null;
}


Value encryptwallet(const Array& params, bool fHelp)
{
    if (!pwalletMain->IsCrypted() && (fHelp || params.size() != 1))
        throw runtime_error(
            "encryptwallet <passphrase>\n"
            "Encrypts the wallet with <passphrase>.");
    if (fHelp)
        return true;
    if (pwalletMain->IsCrypted())
        throw JSONRPCError(-15, "Error: running with an encrypted wallet, but encryptwallet was called.");

    string strWalletPass;
    strWalletPass.reserve(100);
    mlock(&strWalletPass[0], strWalletPass.capacity());
    strWalletPass = params[0].get_str();

    if (strWalletPass.length() < 1)
        throw runtime_error(
            "encryptwallet <passphrase>\n"
            "Encrypts the wallet with <passphrase>.");

    if (!pwalletMain->EncryptWallet(strWalletPass))
    {
        fill(strWalletPass.begin(), strWalletPass.end(), '\0');
        munlock(&strWalletPass[0], strWalletPass.capacity());
        throw JSONRPCError(-16, "Error: Failed to encrypt the wallet.");
    }
    fill(strWalletPass.begin(), strWalletPass.end(), '\0');
    munlock(&strWalletPass[0], strWalletPass.capacity());

    return Value::null;
}


Value validateaddress(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "validateaddress <bitcoinaddress>\n"
            "Return information about <bitcoinaddress>.");

    CBitcoinAddress address(params[0].get_str());
    bool isValid = address.IsValid();

    Object ret;
    ret.push_back(Pair("isvalid", isValid));
    if (isValid)
    {
        // Call Hash160ToAddress() so we always return current ADDRESSVERSION
        // version of the address:
        string currentAddress = address.ToString();
        ret.push_back(Pair("address", currentAddress));
        ret.push_back(Pair("ismine", (pwalletMain->HaveKey(address) > 0)));
        if (pwalletMain->mapAddressBook.count(address))
            ret.push_back(Pair("account", pwalletMain->mapAddressBook[address]));
    }
    return ret;
}

Value getwork(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw runtime_error(
            "getwork [data]\n"
            "If [data] is not specified, returns formatted hash data to work on:\n"
            "  \"midstate\" : precomputed hash state after hashing the first half of the data\n"
            "  \"data\" : block data\n"
            "  \"hash1\" : formatted hash buffer for second hash\n"
            "  \"target\" : little endian hash target\n"
            "If [data] is specified, tries to solve the block and returns true if it was successful.");

    if (vNodes.empty())
        throw JSONRPCError(-9, "Bitcoin is not connected!");

    if (IsInitialBlockDownload())
        throw JSONRPCError(-10, "Bitcoin is downloading blocks...");

    typedef map<uint256, pair<CBlock*, CScript> > mapNewBlock_t;
    static mapNewBlock_t mapNewBlock;
    static vector<CBlock*> vNewBlock;
    static CReserveKey reservekey(pwalletMain);

    if (params.size() == 0)
    {
        // Update block
        static unsigned int nTransactionsUpdatedLast;
        static CBlockIndex* pindexPrev;
        static int64 nStart;
        static CBlock* pblock;
        if (pindexPrev != pindexBest ||
            (nTransactionsUpdated != nTransactionsUpdatedLast && GetTime() - nStart > 60))
        {
            if (pindexPrev != pindexBest)
            {
                // Deallocate old blocks since they're obsolete now
                mapNewBlock.clear();
                BOOST_FOREACH(CBlock* pblock, vNewBlock)
                    delete pblock;
                vNewBlock.clear();
            }
            nTransactionsUpdatedLast = nTransactionsUpdated;
            pindexPrev = pindexBest;
            nStart = GetTime();

            // Create new block
            pblock = CreateNewBlock(reservekey);
            if (!pblock)
                throw JSONRPCError(-7, "Out of memory");
            vNewBlock.push_back(pblock);
        }

        // Update nTime
        pblock->nTime = max(pindexPrev->GetMedianTimePast()+1, GetAdjustedTime());
        pblock->nNonce = 0;

        // Update nExtraNonce
        static unsigned int nExtraNonce = 0;
        IncrementExtraNonce(pblock, pindexPrev, nExtraNonce);

        // Save
        mapNewBlock[pblock->hashMerkleRoot] = make_pair(pblock, pblock->vtx[0].vin[0].scriptSig);

        // Prebuild hash buffers
        char pmidstate[32];
        char pdata[128];
        char phash1[64];
        FormatHashBuffers(pblock, pmidstate, pdata, phash1);

        uint256 hashTarget = CBigNum().SetCompact(pblock->nBits).getuint256();

        Object result;
        result.push_back(Pair("midstate", HexStr(BEGIN(pmidstate), END(pmidstate))));
        result.push_back(Pair("data",     HexStr(BEGIN(pdata), END(pdata))));
        result.push_back(Pair("hash1",    HexStr(BEGIN(phash1), END(phash1))));
        result.push_back(Pair("target",   HexStr(BEGIN(hashTarget), END(hashTarget))));
        return result;
    }
    else
    {
        // Parse parameters
        vector<unsigned char> vchData = ParseHex(params[0].get_str());
        if (vchData.size() != 128)
            throw JSONRPCError(-8, "Invalid parameter");
        CBlock* pdata = (CBlock*)&vchData[0];

        // Byte reverse
        for (int i = 0; i < 128/4; i++)
            ((unsigned int*)pdata)[i] = CryptoPP::ByteReverse(((unsigned int*)pdata)[i]);

        // Get saved block
        if (!mapNewBlock.count(pdata->hashMerkleRoot))
            return false;
        CBlock* pblock = mapNewBlock[pdata->hashMerkleRoot].first;

        pblock->nTime = pdata->nTime;
        pblock->nNonce = pdata->nNonce;
        pblock->vtx[0].vin[0].scriptSig = mapNewBlock[pdata->hashMerkleRoot].second;
        pblock->hashMerkleRoot = pblock->BuildMerkleTree();

        return CheckWork(pblock, *pwalletMain, reservekey);
    }
}
 */

Value gettxdetails(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
                            "gettxdetails <txhash>\n"
                            "Get transaction details for transaction with hash <txhash>");
    
    uint256 hash;
    hash.SetHex(params[0].get_str());
    
    CTxDB txdb("r");
    
    CTransaction tx;
    if(!txdb.ReadDiskTx(hash, tx))
        throw JSONRPCError(-5, "Invalid transaction id");
    
    Object entry;
    
    // scheme follows the scheme of blockexplorer:
    // "hash" : hash in hex
    // "ver" : vernum
    entry.push_back(Pair("hash", hash.ToString()));
    entry.push_back(Pair("ver", tx.nVersion));
    entry.push_back(Pair("vin_sz", uint64_t(tx.vin.size())));
    entry.push_back(Pair("vout_sz", uint64_t(tx.vout.size())));
    entry.push_back(Pair("lock_time", uint64_t(tx.nLockTime)));
    entry.push_back(Pair("size", uint64_t(::GetSerializeSize(tx, SER_NETWORK))));
    
    // now loop over the txins
    Array txins;
    BOOST_FOREACH(const CTxIn& txin, tx.vin)
    {
        Object inentry;
        inentry.clear();
        Object prevout;
        prevout.clear();
        prevout.push_back(Pair("hash", txin.prevout.hash.ToString()));
        prevout.push_back(Pair("n", uint64_t(txin.prevout.n)));
        inentry.push_back(Pair("prev_out", prevout));
        if(tx.IsCoinBase())            
            inentry.push_back(Pair("coinbase", txin.scriptSig.ToString()));
        else
            inentry.push_back(Pair("scriptSig", txin.scriptSig.ToString()));
        txins.push_back(inentry);
    }
    entry.push_back(Pair("in", txins));
    
    // now loop over the txouts
    Array txouts;
    BOOST_FOREACH(const CTxOut& txout, tx.vout)
    {
        Object outentry;
        outentry.clear();
        outentry.push_back(Pair("value", strprintf("%"PRI64d".%08"PRI64d"",txout.nValue/COIN, txout.nValue%COIN))); // format correctly
        outentry.push_back(Pair("scriptPubKey", txout.scriptPubKey.ToString()));
        txouts.push_back(outentry);
    }
    entry.push_back(Pair("out", txouts));
    
    return entry;    
}


bool Solver(const CScript& scriptPubKey, vector<pair<opcodetype, vector<unsigned char> > >& vSolutionRet);

typedef pair<uint256, unsigned int> Coin;

class CAsset;

class CAsset : public CKeyStore
{
public:
    //    typedef COutPoint Coin;
    typedef set<Coin> Coins;
    typedef map<uint256, CTransaction> TxCache;
    typedef set<uint256> TxSet;
    typedef map<uint160, CKey> KeyMap;
private:
    KeyMap _keymap;
    TxCache _tx_cache;
    Coins _coins;
    
public:
    CAsset() {}
    void addAddress(uint160 hash160) { _keymap[hash160]; }
    void addKey(CKey key) { _keymap[Hash160(key.GetPubKey())] = key; }
    
    set<uint160> getAddresses()
    {
        set<uint160> addresses;
        for(KeyMap::iterator addr = _keymap.begin(); addr != _keymap.end(); ++addr)
            addresses.insert(addr->first);
        return addresses;
    }
    
    virtual bool AddKey(const CKey& key)
    {
        _keymap[Hash160(key.GetPubKey())] = key;
        return true;
    }
    
    virtual bool HaveKey(const CBitcoinAddress &address) const
    {
        uint160 hash160 = address.GetHash160();
        return (_keymap.count(hash160) > 0);
    }
    
    virtual bool GetKey(const CBitcoinAddress &address, CKey& keyOut) const
    {
        uint160 hash160 = address.GetHash160();
        KeyMap::const_iterator pair = _keymap.find(hash160);
        if(pair != _keymap.end())
        {
            keyOut = pair->second;
            return true;
        }
        return false;        
    }
    
    // we might need to override these as well... Depending how well we want to support having only keys with pub/160 part
    //    virtual bool GetPubKey(const CBitcoinAddress &address, std::vector<unsigned char>& vchPubKeyOut) const
    //    virtual std::vector<unsigned char> GenerateNewKey();
    
    const CTransaction& getTx(uint256 hash) const
    {
        TxCache::const_iterator pair = _tx_cache.find(hash);
        if(pair != _tx_cache.end())
        {
            return pair->second;
        }
        //        cout << "CACHE MISS!!! " << hash.ToString() << endl;
        /*
         CTxDB txdb("r");
         CTransaction tx;
         if(txdb.ReadDiskTx(hash, tx))
         {
         _tx_cache[hash] = tx;
         return _tx_cache[hash];
         }
         */
        // throw something!
    }
    
    uint160 getAddress(const CTxOut& out)
    {
        vector<pair<opcodetype, vector<unsigned char> > > vSolution;
        if (!Solver(out.scriptPubKey, vSolution))
            return 0;
        
        BOOST_FOREACH(PAIRTYPE(opcodetype, vector<unsigned char>)& item, vSolution)
        {
            vector<unsigned char> vchPubKey;
            if (item.first == OP_PUBKEY)
            {
                // encode the pubkey into a hash160
                return Hash160(item.second);                
            }
            else if (item.first == OP_PUBKEYHASH)
            {
                return uint160(item.second);                
            }
        }
        return 0;
    }
    
    void syncronize()
    {
        CTxDB txdb("r");
        
        _coins.clear();
        
        // read all relevant tx'es
        for(KeyMap::iterator key = _keymap.begin(); key != _keymap.end(); ++key)
        {
            Coins debit;
            txdb.ReadDrIndex(key->first, debit);
            Coins credit;
            txdb.ReadCrIndex(key->first, credit);
            
            Coins coins;
            for(Coins::iterator coin = debit.begin(); coin != debit.end(); ++coin)
            {
                CTransaction tx;
                txdb.ReadDiskTx(coin->first, tx);
                _tx_cache[coin->first] = tx; // caches the relevant transactions
                coins.insert(*coin);
            }
            for(Coins::iterator coin = credit.begin(); coin != credit.end(); ++coin)
            {
                CTransaction tx;
                txdb.ReadDiskTx(coin->first, tx);
                _tx_cache[coin->first] = tx; // caches the relevant transactions
                
                CTxIn in = tx.vin[coin->second];
                Coin spend(in.prevout.hash, in.prevout.n);
                coins.erase(spend);
            }
            
            _coins.insert(coins.begin(), coins.end());
        }
        
        // now _coins contains all non-spend coins !
    }
    
    const Coins& getCoins()
    {
        return _coins;
    }
    
    bool isSpendable(Coin coin)
    {
        // get the address
        CTransaction tx = getTx(coin.first);
        CTxOut& out = tx.vout[coin.second];
        uint160 addr = getAddress(out);
        KeyMap::iterator keypair = _keymap.find(addr);
        if(keypair->second.IsNull())
            return false;
        
        try {
            CPrivKey privkey = keypair->second.GetPrivKey();
        }
        catch(key_error err)
        {
            return false;
        }
        return true;        
    }
    
    const int64 value(Coin coin) const
    {
        const CTransaction& tx = getTx(coin.first);
        const CTxOut& out = tx.vout[coin.second];
        return out.nValue;
    }
    
    struct CompValue {
        const CAsset& _asset;
        CompValue(const CAsset& asset) : _asset(asset) {}
        bool operator() (Coin a, Coin b)
        {
            return (_asset.value(a) < _asset.value(b));
        }
    };
    
    int64 balance()
    {
        int64 sum = 0;
        for(Coins::iterator coin = _coins.begin(); coin != _coins.end(); ++coin)
            sum += value(*coin);
        return sum;
    }
    
    int64 spendable_balance()
    {
        int64 sum = 0;
        for(Coins::iterator coin = _coins.begin(); coin != _coins.end(); ++coin)
            if(isSpendable(*coin))
                sum += value(*coin);
        return sum;
    }
    
    typedef pair<uint160, int64> Payment;
    CTransaction generateTx(set<Payment> payments, uint160 changeaddr = 0)
    {
        int64 amount = 0;
        for(set<Payment>::iterator payment = payments.begin(); payment != payments.end(); ++payment)
            amount += payment->second;
        
        // calculate fee
        int64 fee = max(amount/1000, (int64)500000);
        amount += fee;
        
        // create a list of spendable coins
        vector<Coin> spendable_coins;
        for(Coins::iterator coin = _coins.begin(); coin != _coins.end(); ++coin)
            if (isSpendable(*coin)) {
                spendable_coins.push_back(*coin);
            }
        
        CompValue compvalue(*this);
        sort(spendable_coins.begin(), spendable_coins.end(), compvalue);
        
        int64 sum = 0;
        int coins = 0;
        for(coins = 0; coins < spendable_coins.size(); coins++)
        {
            sum += value(spendable_coins[coins]);
            if(sum > amount)
                break;
        }
        
        CTransaction tx;
        
        if(sum < amount) // could not pay - throw something
            return tx;
        
        // shuffle the inputs
        random_shuffle(spendable_coins.begin(), spendable_coins.end());
        
        // now fill in the tx outs
        for(set<Payment>::iterator payment = payments.begin(); payment != payments.end(); ++payment)
        {
            CScript scriptPubKey;
            scriptPubKey << OP_DUP << OP_HASH160 << payment->first << OP_EQUALVERIFY << OP_CHECKSIG;
            
            CTxOut out(payment->second, scriptPubKey);
            tx.vout.push_back(out);
        }            
        // handle change!
        int64 change = amount - sum;
        uint160 changeto = changeaddr;
        if(changeto == 0)
        {
            CTransaction txchange = getTx(spendable_coins[0].first);
            uint160 changeto = getAddress(txchange.vout[spendable_coins[0].second]);
        }
        
        if(change > 50000) // skip smaller amounts of change
        {
            CScript scriptPubKey;
            scriptPubKey << OP_DUP << OP_HASH160 << changeto << OP_EQUALVERIFY << OP_CHECKSIG;
            
            CTxOut out(change, scriptPubKey);
            tx.vout.push_back(out);
        }            
        
        int64 invalue = 0;
        for(int i = 0; i < coins; i++)
        {
            const Coin& coin = spendable_coins[i];
            invalue += value(spendable_coins[i]);
            CTxIn in(coin.first, coin.second);
            
            tx.vin.push_back(in);
        }
        
        for(int i = 0; i < coins; i++)
        {
            const Coin& coin = spendable_coins[i];
            const CTransaction& txfrom = getTx(coin.first);
            if (!SignSignature(*this, txfrom, tx, i))
            {
                // throw something!                
            }
        }        
        return tx;
    }
    
    CTransaction generateTx(uint160 to, int64 amount, uint160 change = 0)
    {
        set<Payment> payments;
        payments.insert(Payment(to, amount));
        return generateTx(payments, change);
    }
};

Value getvalue(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
                            "getvalue <btcaddr>\n"
                            "Get value of <btcaddr>");
    
    uint160 hash160 = CBitcoinAddress(params[0].get_str()).GetHash160();
    
    CAsset asset;
    asset.addAddress(hash160);
    asset.syncronize();
    int64 balance = asset.balance();
    
    Value val(balance);
    
    return val;
}
/*
Value gettxto(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 2)
        throw runtime_error(
                            "gettxto <value> <btcaddr>\n"
                            "Get transaction of <value> to <btcaddr>");
    
    double dvalue;
    istringstream(params[0].get_str()) >> dvalue;
    int64 nValue = COIN*dvalue;
    CBitcoinAddress address(params[1].get_str());
    
    CScript scriptPubKey;
    scriptPubKey.SetBitcoinAddress(address);
    
    CWalletTx wtx;
    
    CReserveKey reservekey(pwalletMain);
    int64 nFeeRequired;
    
    if (! pwalletMain->CreateTransaction(scriptPubKey, nValue, wtx, reservekey, nFeeRequired))
    {
        string strError;
        if (nValue + nFeeRequired > pwalletMain->GetBalance())
            strError = strprintf(_("Error: This transaction requires a transaction fee of at least %s because of its amount, complexity, or use of recently received funds  "), FormatMoney(nFeeRequired).c_str());
        else
            strError = _("Error: Transaction creation failed  ");
        throw JSONRPCError(-5, strError);
    }
    
    CTransaction tx = wtx;
    
    // or using PorteMonnaie instead:
    // CPorteMonnaie porteMonnaie("<path-to-coins>"); // the folder contains files of name <bitcoinaddress>.pem containing, possibly encoded, private ecc keys. The file can be empty in which case the coin is assumed to be of observation only. - further, portemonnaie assumes a cache - named similarly as bitcoinaddress.cache containing the collected tx'es
    // portemonnaie.syncronize(txdb);
    // CTransaction tx = porteMonnaie.pay(nValue, address);    
    
    Object entry;
    
    // scheme follows the scheme of blockexplorer:
    // "ver" : vernum
    entry.push_back(Pair("hash", tx.GetHash().ToString()));
    entry.push_back(Pair("ver", tx.nVersion));
    entry.push_back(Pair("vin_sz", uint64_t(tx.vin.size())));
    entry.push_back(Pair("vout_sz", uint64_t(tx.vout.size())));
    entry.push_back(Pair("lock_time", uint64_t(tx.nLockTime)));
    entry.push_back(Pair("size", uint64_t(::GetSerializeSize(tx, SER_NETWORK))));
    
    // now loop over the txins
    Array txins;
    BOOST_FOREACH(const CTxIn& txin, tx.vin)
    {
        Object inentry;
        inentry.clear();
        Object prevout;
        prevout.clear();
        prevout.push_back(Pair("hash", txin.prevout.hash.ToString()));
        prevout.push_back(Pair("n", uint64_t(txin.prevout.n)));
        inentry.push_back(Pair("prev_out", prevout));
        if(tx.IsCoinBase())            
            inentry.push_back(Pair("coinbase", txin.scriptSig.ToString()));
        else
            inentry.push_back(Pair("scriptSig", txin.scriptSig.ToString()));
        txins.push_back(inentry);
    }
    entry.push_back(Pair("in", txins));
    
    // now loop over the txouts
    Array txouts;
    BOOST_FOREACH(const CTxOut& txout, tx.vout)
    {
        Object outentry;
        outentry.clear();
        outentry.push_back(Pair("value", strprintf("%"PRI64d".%08"PRI64d"",txout.nValue/COIN, txout.nValue%COIN))); // format correctly
        outentry.push_back(Pair("scriptPubKey", txout.scriptPubKey.ToString()));
        txouts.push_back(outentry);
    }
    entry.push_back(Pair("out", txouts));
    
    return entry;    
}    
*/
Value getdebit(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
                            "getdebit <btcaddr>\n"
                            "Get debit coins of <btcaddr>");
    uint160 hash160 = CBitcoinAddress(params[0].get_str()).GetHash160();
    
    CTxDB txdb("r");
    
    set<Coin> debit;
    
    txdb.ReadDrIndex(hash160, debit);
    
    Array list;
    
    for(set<Coin>::iterator coin = debit.begin(); coin != debit.end(); ++coin)
    {
        Object obj;
        obj.clear();
        obj.push_back(Pair("hash", coin->first.ToString()));
        obj.push_back(Pair("n", uint64_t(coin->second)));
        list.push_back(obj);
    }
    
    return list;
}

Value getcredit(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
                            "getcredit <btcaddr>\n"
                            "Get credit coins of <btcaddr>");
    uint160 hash160 = CBitcoinAddress(params[0].get_str()).GetHash160();
    
    CTxDB txdb("r");
    
    set<Coin> credit;
    
    txdb.ReadCrIndex(hash160, credit);
    
    Array list;
    
    for(set<Coin>::iterator coin = credit.begin(); coin != credit.end(); ++coin)
    {
        Object obj;
        obj.clear();
        obj.push_back(Pair("hash", coin->first.ToString()));
        obj.push_back(Pair("n", uint64_t(coin->second)));
        list.push_back(obj);
    }
    
    return list;
}

Value getcoins(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
                            "getcoins <btcaddr>\n"
                            "Get non spend coins of <btcaddr>");
    
    uint160 hash160 = CBitcoinAddress(params[0].get_str()).GetHash160();
    
    CAsset asset;
    asset.addAddress(hash160);
    asset.syncronize();
    set<Coin> coins = asset.getCoins();
    
    Array list;
    
    for(set<Coin>::iterator coin = coins.begin(); coin != coins.end(); ++coin)
    {
        Object obj;
        obj.clear();
        obj.push_back(Pair("hash", coin->first.ToString()));
        obj.push_back(Pair("n", uint64_t(coin->second)));
        list.push_back(obj);
    }
    
    return list;
}

Value posttx(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
                            "posttx <tx>\n"
                            "Post a signed transaction json"); // note this is a new transaction and hence it has no hash!
    
    Object entry = params[0].get_obj();
    
    CTransaction tx;
    
    // find first the version
    tx.nVersion = find_value(entry, "ver").get_int();
    // then the lock_time
    tx.nLockTime = find_value(entry, "lock_time").get_int();
    
    // then find the vins and outs
    Array txins = find_value(entry, "in").get_array();
    Array txouts = find_value(entry, "out").get_array();
    
    map<string, opcodetype> opcodemap; // the following will create a map opcodeName -> opCode, the opcodeName='OP_UNKNOWN' = 0xfc (last code with no name)
    for(unsigned char c = 0; c < UCHAR_MAX; c++) opcodemap[string(GetOpName(opcodetype(c)))] = (opcodetype)c;
    
    for(Array::iterator itin = txins.begin(); itin != txins.end(); ++itin)
    {
        Object in = itin->get_obj();
        Object prev_out = find_value(in, "prev_out").get_obj();
        uint256 hash(find_value(prev_out, "hash").get_str());
        unsigned int n = find_value(prev_out, "n").get_int();
        CScript scriptSig;
        string sscript = find_value(in, "scriptSig").get_str();
        // traverse through the vector and pushback opcodes and values
        istringstream iss(sscript);
        string token;
        while(iss >> token)
        {
            map<string, opcodetype>::iterator opcode = opcodemap.find(token);
            if(opcode != opcodemap.end()) // opcode read
                scriptSig << opcode->second;
            else // value read
                scriptSig << ParseHex(token);
        }
        CTxIn txin(hash, n, scriptSig);
        tx.vin.push_back(txin);
    }
    
    for(Array::iterator itout = txouts.begin(); itout != txouts.end(); ++itout)
    {
        Object out = itout->get_obj();
        string strvalue = find_value(out, "value").get_str();
        //parse string value...
        istringstream ivs(strvalue);
        double dvalue;
        ivs >> dvalue;
        dvalue *= COIN;
        int64 value = dvalue;
        CScript scriptPubkey;
        string sscript = find_value(out, "scriptPubKey").get_str();
        // traverse through the vector and pushback opcodes and values
        istringstream iss(sscript);
        string token;
        while(iss >> token)
        {
            map<string, opcodetype>::iterator opcode = opcodemap.find(token);
            if(opcode != opcodemap.end()) // opcode read
                scriptPubkey << opcode->second;
            else // value read
                scriptPubkey << ParseHex(token);
        }
        CTxOut txout(value, scriptPubkey);
        tx.vout.push_back(txout);
    }
    
    // now we have read the transaction - we need verify and possibly post it to the p2p network
    // We create a fake node and pretend we got this from the network - this ensures that this is handled by the right thread...
    CNode* pnode = new CNode(INVALID_SOCKET, CAddress("localhost"));
    
    CDataStream ss;
    ss << tx;
    CMessageHeader header("tx", ss.size());
    uint256 hash = Hash(ss.begin(), ss.begin() + ss.size());
    unsigned int nChecksum;
    memcpy(&nChecksum, &hash, sizeof(nChecksum));    
    header.nChecksum = nChecksum;
    pnode->vRecv << header;
    pnode->vRecv << tx;
    pnode->nVersion = VERSION;
    
    CRITICAL_BLOCK(cs_vNodes)
    {
        pnode->nTimeConnected = GetTime();
        pnode->nLastRecv = GetTime();
        pnode->AddRef();
        vNodes.push_back(pnode); // add the node to the node list - it will then get polled for its content and purged at the next occation
    }
    
    return Value::null;
}


//
// Call Table
//

pair<string, rpcfn_type> pCallTable[] =
{
    make_pair("help",                   &help),
    make_pair("stop",                   &stop),
    make_pair("getblockcount",          &getblockcount),
    make_pair("getblocknumber",         &getblocknumber),
    make_pair("getconnectioncount",     &getconnectioncount),
    make_pair("getdifficulty",          &getdifficulty),
    /*
    make_pair("getgenerate",            &getgenerate),
    make_pair("setgenerate",            &setgenerate),
     */
    make_pair("gethashespersec",        &gethashespersec),
    make_pair("getinfo",                &getinfo),
    /*
    make_pair("getnewaddress",          &getnewaddress),
    make_pair("getaccountaddress",      &getaccountaddress),
    make_pair("setaccount",             &setaccount),
    make_pair("setlabel",               &setaccount), // deprecated
    make_pair("getaccount",             &getaccount),
    make_pair("getlabel",               &getaccount), // deprecated
    make_pair("getaddressesbyaccount",  &getaddressesbyaccount),
    make_pair("getaddressesbylabel",    &getaddressesbyaccount), // deprecated
    make_pair("sendtoaddress",          &sendtoaddress),
    make_pair("getamountreceived",      &getreceivedbyaddress), // deprecated, renamed to getreceivedbyaddress
    make_pair("getallreceived",         &listreceivedbyaddress), // deprecated, renamed to listreceivedbyaddress
    make_pair("getreceivedbyaddress",   &getreceivedbyaddress),
    make_pair("getreceivedbyaccount",   &getreceivedbyaccount),
    make_pair("getreceivedbylabel",     &getreceivedbyaccount), // deprecated
    make_pair("listreceivedbyaddress",  &listreceivedbyaddress),
    make_pair("listreceivedbyaccount",  &listreceivedbyaccount),
    make_pair("listreceivedbylabel",    &listreceivedbyaccount), // deprecated
    make_pair("backupwallet",           &backupwallet),
    make_pair("keypoolrefill",          &keypoolrefill),
    make_pair("walletpassphrase",       &walletpassphrase),
    make_pair("walletpassphrasechange", &walletpassphrasechange),
    make_pair("walletlock",             &walletlock),
    make_pair("encryptwallet",          &encryptwallet),
    make_pair("validateaddress",        &validateaddress),
    make_pair("getbalance",             &getbalance),
    make_pair("move",                   &movecmd),
    make_pair("sendfrom",               &sendfrom),
    make_pair("sendmany",               &sendmany),
    make_pair("gettransaction",         &gettransaction),
    make_pair("listtransactions",       &listtransactions),
    make_pair("getwork",                &getwork),
    make_pair("listaccounts",           &listaccounts),
    make_pair("settxfee",               &settxfee),
    */
    make_pair("getdebit",               &getdebit),
    make_pair("getcredit",              &getcredit),
    make_pair("getcoins",               &getcoins),
    make_pair("gettxdetails",           &gettxdetails),
    make_pair("posttx",                 &posttx),
//    make_pair("gettxto",                &gettxto),
};
map<string, rpcfn_type> mapCallTable(pCallTable, pCallTable + sizeof(pCallTable)/sizeof(pCallTable[0]));

string pAllowInSafeMode[] =
{
    "help",
    "stop",
    "getblockcount",
    "getblocknumber",
    "getconnectioncount",
    "getdifficulty",
//    "getgenerate",
//    "setgenerate",
    "gethashespersec",
    "getinfo",
/*
    "getnewaddress",
    "getaccountaddress",
    "setlabel", // deprecated
    "getaccount",
    "getlabel", // deprecated
    "getaddressesbyaccount",
    "getaddressesbylabel", // deprecated
    "backupwallet",
    "keypoolrefill",
    "walletpassphrase",
    "walletlock",
    "validateaddress",
    "getwork",
*/    
    "getdebit",
    "getcredit",
    "getcoins",
    "gettxdetails",
    "posttx",
};
set<string> setAllowInSafeMode(pAllowInSafeMode, pAllowInSafeMode + sizeof(pAllowInSafeMode)/sizeof(pAllowInSafeMode[0]));




//
// HTTP protocol
//
// This ain't Apache.  We're just using HTTP header for the length field
// and to be compatible with other JSON-RPC implementations.
//

string HTTPPost(const string& strMsg, const map<string,string>& mapRequestHeaders)
{
    ostringstream s;
    s << "POST / HTTP/1.1\r\n"
      << "User-Agent: bitcoin-json-rpc/" << FormatFullVersion() << "\r\n"
      << "Host: 127.0.0.1\r\n"
      << "Content-Type: application/json\r\n"
      << "Content-Length: " << strMsg.size() << "\r\n"
      << "Accept: application/json\r\n";
    BOOST_FOREACH(const PAIRTYPE(string, string)& item, mapRequestHeaders)
        s << item.first << ": " << item.second << "\r\n";
    s << "\r\n" << strMsg;

    return s.str();
}

string rfc1123Time()
{
    char buffer[64];
    time_t now;
    time(&now);
    struct tm* now_gmt = gmtime(&now);
    string locale(setlocale(LC_TIME, NULL));
    setlocale(LC_TIME, "C"); // we want posix (aka "C") weekday/month strings
    strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S +0000", now_gmt);
    setlocale(LC_TIME, locale.c_str());
    return string(buffer);
}

static string HTTPReply(int nStatus, const string& strMsg)
{
    if (nStatus == 401)
        return strprintf("HTTP/1.0 401 Authorization Required\r\n"
            "Date: %s\r\n"
            "Server: bitcoin-json-rpc/%s\r\n"
            "WWW-Authenticate: Basic realm=\"jsonrpc\"\r\n"
            "Content-Type: text/html\r\n"
            "Content-Length: 296\r\n"
            "\r\n"
            "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\"\r\n"
            "\"http://www.w3.org/TR/1999/REC-html401-19991224/loose.dtd\">\r\n"
            "<HTML>\r\n"
            "<HEAD>\r\n"
            "<TITLE>Error</TITLE>\r\n"
            "<META HTTP-EQUIV='Content-Type' CONTENT='text/html; charset=ISO-8859-1'>\r\n"
            "</HEAD>\r\n"
            "<BODY><H1>401 Unauthorized.</H1></BODY>\r\n"
            "</HTML>\r\n", rfc1123Time().c_str(), FormatFullVersion().c_str());
    string strStatus;
         if (nStatus == 200) strStatus = "OK";
    else if (nStatus == 400) strStatus = "Bad Request";
    else if (nStatus == 403) strStatus = "Forbidden";
    else if (nStatus == 404) strStatus = "Not Found";
    else if (nStatus == 500) strStatus = "Internal Server Error";
    return strprintf(
            "HTTP/1.1 %d %s\r\n"
            "Date: %s\r\n"
            "Connection: close\r\n"
            "Content-Length: %d\r\n"
            "Content-Type: application/json\r\n"
            "Server: bitcoin-json-rpc/%s\r\n"
            "\r\n"
            "%s",
        nStatus,
        strStatus.c_str(),
        rfc1123Time().c_str(),
        strMsg.size(),
        FormatFullVersion().c_str(),
        strMsg.c_str());
}

int ReadHTTPStatus(std::basic_istream<char>& stream)
{
    string str;
    getline(stream, str);
    vector<string> vWords;
    boost::split(vWords, str, boost::is_any_of(" "));
    if (vWords.size() < 2)
        return 500;
    return atoi(vWords[1].c_str());
}

int ReadHTTPHeader(std::basic_istream<char>& stream, map<string, string>& mapHeadersRet)
{
    int nLen = 0;
    loop
    {
        string str;
        std::getline(stream, str);
        if (str.empty() || str == "\r")
            break;
        string::size_type nColon = str.find(":");
        if (nColon != string::npos)
        {
            string strHeader = str.substr(0, nColon);
            boost::trim(strHeader);
            boost::to_lower(strHeader);
            string strValue = str.substr(nColon+1);
            boost::trim(strValue);
            mapHeadersRet[strHeader] = strValue;
            if (strHeader == "content-length")
                nLen = atoi(strValue.c_str());
        }
    }
    return nLen;
}

int ReadHTTP(std::basic_istream<char>& stream, map<string, string>& mapHeadersRet, string& strMessageRet)
{
    mapHeadersRet.clear();
    strMessageRet = "";

    // Read status
    int nStatus = ReadHTTPStatus(stream);

    // Read header
    int nLen = ReadHTTPHeader(stream, mapHeadersRet);
    if (nLen < 0 || nLen > MAX_SIZE)
        return 500;

    // Read message
    if (nLen > 0)
    {
        vector<char> vch(nLen);
        stream.read(&vch[0], nLen);
        strMessageRet = string(vch.begin(), vch.end());
    }

    return nStatus;
}

string EncodeBase64(string s)
{
    BIO *b64, *bmem;
    BUF_MEM *bptr;

    b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    bmem = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64, bmem);
    BIO_write(b64, s.c_str(), s.size());
    BIO_flush(b64);
    BIO_get_mem_ptr(b64, &bptr);

    string result(bptr->data, bptr->length);
    BIO_free_all(b64);

    return result;
}

string DecodeBase64(string s)
{
    BIO *b64, *bmem;

    char* buffer = static_cast<char*>(calloc(s.size(), sizeof(char)));

    b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    bmem = BIO_new_mem_buf(const_cast<char*>(s.c_str()), s.size());
    bmem = BIO_push(b64, bmem);
    BIO_read(bmem, buffer, s.size());
    BIO_free_all(bmem);

    string result(buffer);
    free(buffer);
    return result;
}

bool HTTPAuthorized(map<string, string>& mapHeaders)
{
    string strAuth = mapHeaders["authorization"];
    if (strAuth.substr(0,6) != "Basic ")
        return false;
    string strUserPass64 = strAuth.substr(6); boost::trim(strUserPass64);
    string strUserPass = DecodeBase64(strUserPass64);
    string::size_type nColon = strUserPass.find(":");
    if (nColon == string::npos)
        return false;
    string strUser = strUserPass.substr(0, nColon);
    string strPassword = strUserPass.substr(nColon+1);
    return (strUser == mapArgs["-rpcuser"] && strPassword == mapArgs["-rpcpassword"]);
}

//
// JSON-RPC protocol.  Bitcoin speaks version 1.0 for maximum compatibility,
// but uses JSON-RPC 1.1/2.0 standards for parts of the 1.0 standard that were
// unspecified (HTTP errors and contents of 'error').
//
// 1.0 spec: http://json-rpc.org/wiki/specification
// 1.2 spec: http://groups.google.com/group/json-rpc/web/json-rpc-over-http
// http://www.codeproject.com/KB/recipes/JSON_Spirit.aspx
//

string JSONRPCRequest(const string& strMethod, const Array& params, const Value& id)
{
    Object request;
    request.push_back(Pair("method", strMethod));
    request.push_back(Pair("params", params));
    request.push_back(Pair("id", id));
    return write_string(Value(request), false) + "\n";
}

string JSONRPCReply(const Value& result, const Value& error, const Value& id)
{
    Object reply;
    if (error.type() != null_type)
        reply.push_back(Pair("result", Value::null));
    else
        reply.push_back(Pair("result", result));
    reply.push_back(Pair("error", error));
    reply.push_back(Pair("id", id));
    return write_string(Value(reply), false) + "\n";
}

void ErrorReply(std::ostream& stream, const Object& objError, const Value& id)
{
    // Send error reply from json-rpc error object
    int nStatus = 500;
    int code = find_value(objError, "code").get_int();
    if (code == -32600) nStatus = 400;
    else if (code == -32601) nStatus = 404;
    string strReply = JSONRPCReply(Value::null, objError, id);
    stream << HTTPReply(nStatus, strReply) << std::flush;
}

bool ClientAllowed(const string& strAddress)
{
    if (strAddress == asio::ip::address_v4::loopback().to_string())
        return true;
    const vector<string>& vAllow = mapMultiArgs["-rpcallowip"];
    BOOST_FOREACH(string strAllow, vAllow)
        if (WildcardMatch(strAddress, strAllow))
            return true;
    return false;
}

#ifdef USE_SSL
//
// IOStream device that speaks SSL but can also speak non-SSL
//
class SSLIOStreamDevice : public iostreams::device<iostreams::bidirectional> {
public:
    SSLIOStreamDevice(SSLStream &streamIn, bool fUseSSLIn) : stream(streamIn)
    {
        fUseSSL = fUseSSLIn;
        fNeedHandshake = fUseSSLIn;
    }

    void handshake(ssl::stream_base::handshake_type role)
    {
        if (!fNeedHandshake) return;
        fNeedHandshake = false;
        stream.handshake(role);
    }
    std::streamsize read(char* s, std::streamsize n)
    {
        handshake(ssl::stream_base::server); // HTTPS servers read first
        if (fUseSSL) return stream.read_some(asio::buffer(s, n));
        return stream.next_layer().read_some(asio::buffer(s, n));
    }
    std::streamsize write(const char* s, std::streamsize n)
    {
        handshake(ssl::stream_base::client); // HTTPS clients write first
        if (fUseSSL) return asio::write(stream, asio::buffer(s, n));
        return asio::write(stream.next_layer(), asio::buffer(s, n));
    }
    bool connect(const std::string& server, const std::string& port)
    {
        ip::tcp::resolver resolver(stream.get_io_service());
        ip::tcp::resolver::query query(server.c_str(), port.c_str());
        ip::tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);
        ip::tcp::resolver::iterator end;
        boost::system::error_code error = asio::error::host_not_found;
        while (error && endpoint_iterator != end)
        {
            stream.lowest_layer().close();
            stream.lowest_layer().connect(*endpoint_iterator++, error);
        }
        if (error)
            return false;
        return true;
    }

private:
    bool fNeedHandshake;
    bool fUseSSL;
    SSLStream& stream;
};
#endif

void ThreadRPCServer(void* parg)
{
    IMPLEMENT_RANDOMIZE_STACK(ThreadRPCServer(parg));
    try
    {
        vnThreadsRunning[4]++;
        ThreadRPCServer2(parg);
        vnThreadsRunning[4]--;
    }
    catch (std::exception& e) {
        vnThreadsRunning[4]--;
        PrintException(&e, "ThreadRPCServer()");
    } catch (...) {
        vnThreadsRunning[4]--;
        PrintException(NULL, "ThreadRPCServer()");
    }
    printf("ThreadRPCServer exiting\n");
}

void ThreadRPCServer2(void* parg)
{
    printf("ThreadRPCServer started\n");

    if (mapArgs["-rpcuser"] == "" && mapArgs["-rpcpassword"] == "")
    {
        string strWhatAmI = "To use bitcoind";
        if (mapArgs.count("-server"))
            strWhatAmI = strprintf(_("To use the %s option"), "\"-server\"");
        else if (mapArgs.count("-daemon"))
            strWhatAmI = strprintf(_("To use the %s option"), "\"-daemon\"");
        PrintConsole(
            _("Warning: %s, you must set rpcpassword=<password>\nin the configuration file: %s\n"
              "If the file does not exist, create it with owner-readable-only file permissions.\n"),
                strWhatAmI.c_str(),
                GetConfigFile().c_str());
        CreateThread(Shutdown, NULL);
        return;
    }

    bool fUseSSL = GetBoolArg("-rpcssl");
    asio::ip::address bindAddress = mapArgs.count("-rpcallowip") ? asio::ip::address_v4::any() : asio::ip::address_v4::loopback();

    asio::io_service io_service;
    ip::tcp::endpoint endpoint(bindAddress, GetArg("-rpcport", 8332));
    ip::tcp::acceptor acceptor(io_service, endpoint);

    acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));

#ifdef USE_SSL
    ssl::context context(io_service, ssl::context::sslv23);
    if (fUseSSL)
    {
        context.set_options(ssl::context::no_sslv2);
        filesystem::path certfile = GetArg("-rpcsslcertificatechainfile", "server.cert");
        if (!certfile.is_complete()) certfile = filesystem::path(GetDataDir()) / certfile;
        if (filesystem::exists(certfile)) context.use_certificate_chain_file(certfile.string().c_str());
        else printf("ThreadRPCServer ERROR: missing server certificate file %s\n", certfile.string().c_str());
        filesystem::path pkfile = GetArg("-rpcsslprivatekeyfile", "server.pem");
        if (!pkfile.is_complete()) pkfile = filesystem::path(GetDataDir()) / pkfile;
        if (filesystem::exists(pkfile)) context.use_private_key_file(pkfile.string().c_str(), ssl::context::pem);
        else printf("ThreadRPCServer ERROR: missing server private key file %s\n", pkfile.string().c_str());

        string ciphers = GetArg("-rpcsslciphers",
                                         "TLSv1+HIGH:!SSLv2:!aNULL:!eNULL:!AH:!3DES:@STRENGTH");
        SSL_CTX_set_cipher_list(context.impl(), ciphers.c_str());
    }
#else
    if (fUseSSL)
        throw runtime_error("-rpcssl=1, but bitcoin compiled without full openssl libraries.");
#endif

    loop
    {
        // Accept connection
#ifdef USE_SSL
        SSLStream sslStream(io_service, context);
        SSLIOStreamDevice d(sslStream, fUseSSL);
        iostreams::stream<SSLIOStreamDevice> stream(d);
#else
        ip::tcp::iostream stream;
#endif

        ip::tcp::endpoint peer;
        vnThreadsRunning[4]--;
#ifdef USE_SSL
        acceptor.accept(sslStream.lowest_layer(), peer);
#else
        acceptor.accept(*stream.rdbuf(), peer);
#endif
        vnThreadsRunning[4]++;
        if (fShutdown)
            return;

        // Restrict callers by IP
        if (!ClientAllowed(peer.address().to_string()))
        {
            // Only send a 403 if we're not using SSL to prevent a DoS during the SSL handshake.
            if (!fUseSSL)
                stream << HTTPReply(403, "") << std::flush;
            continue;
        }

        map<string, string> mapHeaders;
        string strRequest;

        boost::thread api_caller(ReadHTTP, boost::ref(stream), boost::ref(mapHeaders), boost::ref(strRequest));
        if (!api_caller.timed_join(boost::posix_time::seconds(GetArg("-rpctimeout", 30))))
        {   // Timed out:
            acceptor.cancel();
            printf("ThreadRPCServer ReadHTTP timeout\n");
            continue;
        }

        // Check authorization
        if (mapHeaders.count("authorization") == 0)
        {
            stream << HTTPReply(401, "") << std::flush;
            continue;
        }
        if (!HTTPAuthorized(mapHeaders))
        {
            // Deter brute-forcing short passwords
            if (mapArgs["-rpcpassword"].size() < 15)
                Sleep(50);

            stream << HTTPReply(401, "") << std::flush;
            printf("ThreadRPCServer incorrect password attempt\n");
            continue;
        }

        Value id = Value::null;
        try
        {
            // Parse request
            Value valRequest;
            if (!read_string(strRequest, valRequest) || valRequest.type() != obj_type)
                throw JSONRPCError(-32700, "Parse error");
            const Object& request = valRequest.get_obj();

            // Parse id now so errors from here on will have the id
            id = find_value(request, "id");

            // Parse method
            Value valMethod = find_value(request, "method");
            if (valMethod.type() == null_type)
                throw JSONRPCError(-32600, "Missing method");
            if (valMethod.type() != str_type)
                throw JSONRPCError(-32600, "Method must be a string");
            string strMethod = valMethod.get_str();
            if (strMethod != "getwork")
                printf("ThreadRPCServer method=%s\n", strMethod.c_str());

            // Parse params
            Value valParams = find_value(request, "params");
            Array params;
            if (valParams.type() == array_type)
                params = valParams.get_array();
            else if (valParams.type() == null_type)
                params = Array();
            else
                throw JSONRPCError(-32600, "Params must be an array");

            // Find method
            map<string, rpcfn_type>::iterator mi = mapCallTable.find(strMethod);
            if (mi == mapCallTable.end())
                throw JSONRPCError(-32601, "Method not found");

            // Observe safe mode
            string strWarning = GetWarnings("rpc");
            if (strWarning != "" && !GetBoolArg("-disablesafemode") && !setAllowInSafeMode.count(strMethod))
                throw JSONRPCError(-2, string("Safe mode: ") + strWarning);

            try
            {
                // Execute
                Value result;
                CRITICAL_BLOCK(cs_main)
//                CRITICAL_BLOCK(pwalletMain->cs_wallet)
                    result = (*(*mi).second)(params, false);

                // Send reply
                string strReply = JSONRPCReply(result, Value::null, id);
                stream << HTTPReply(200, strReply) << std::flush;
            }
            catch (std::exception& e)
            {
                ErrorReply(stream, JSONRPCError(-1, e.what()), id);
            }
        }
        catch (Object& objError)
        {
            ErrorReply(stream, objError, id);
        }
        catch (std::exception& e)
        {
            ErrorReply(stream, JSONRPCError(-32700, e.what()), id);
        }
    }
}




Object CallRPC(const string& strMethod, const Array& params)
{
    if (mapArgs["-rpcuser"] == "" && mapArgs["-rpcpassword"] == "")
        throw runtime_error(strprintf(
            _("You must set rpcpassword=<password> in the configuration file:\n%s\n"
              "If the file does not exist, create it with owner-readable-only file permissions."),
                GetConfigFile().c_str()));

    // Connect to localhost
    bool fUseSSL = GetBoolArg("-rpcssl");
#ifdef USE_SSL
    asio::io_service io_service;
    ssl::context context(io_service, ssl::context::sslv23);
    context.set_options(ssl::context::no_sslv2);
    SSLStream sslStream(io_service, context);
    SSLIOStreamDevice d(sslStream, fUseSSL);
    iostreams::stream<SSLIOStreamDevice> stream(d);
    if (!d.connect(GetArg("-rpcconnect", "127.0.0.1"), GetArg("-rpcport", "8332")))
        throw runtime_error("couldn't connect to server");
#else
    if (fUseSSL)
        throw runtime_error("-rpcssl=1, but bitcoin compiled without full openssl libraries.");

    ip::tcp::iostream stream(GetArg("-rpcconnect", "127.0.0.1"), GetArg("-rpcport", "8332"));
    if (stream.fail())
        throw runtime_error("couldn't connect to server");
#endif


    // HTTP basic authentication
    string strUserPass64 = EncodeBase64(mapArgs["-rpcuser"] + ":" + mapArgs["-rpcpassword"]);
    map<string, string> mapRequestHeaders;
    mapRequestHeaders["Authorization"] = string("Basic ") + strUserPass64;

    // Send request
    string strRequest = JSONRPCRequest(strMethod, params, 1);
    string strPost = HTTPPost(strRequest, mapRequestHeaders);
    stream << strPost << std::flush;

    // Receive reply
    map<string, string> mapHeaders;
    string strReply;
    int nStatus = ReadHTTP(stream, mapHeaders, strReply);
    if (nStatus == 401)
        throw runtime_error("incorrect rpcuser or rpcpassword (authorization failed)");
    else if (nStatus >= 400 && nStatus != 400 && nStatus != 404 && nStatus != 500)
        throw runtime_error(strprintf("server returned HTTP error %d", nStatus));
    else if (strReply.empty())
        throw runtime_error("no response from server");

    // Parse reply
    Value valReply;
    if (!read_string(strReply, valReply))
        throw runtime_error("couldn't parse reply from server");
    const Object& reply = valReply.get_obj();
    if (reply.empty())
        throw runtime_error("expected reply to have result, error and id properties");

    return reply;
}




template<typename T>
void ConvertTo(Value& value)
{
    if (value.type() == str_type)
    {
        // reinterpret string as unquoted json value
        Value value2;
        if (!read_string(value.get_str(), value2))
            throw runtime_error("type mismatch");
        value = value2.get_value<T>();
    }
    else
    {
        value = value.get_value<T>();
    }
}

int CommandLineRPC(int argc, char *argv[])
{
    string strPrint;
    int nRet = 0;
    try
    {
        // Skip switches
        while (argc > 1 && IsSwitchChar(argv[1][0]))
        {
            argc--;
            argv++;
        }

        // Method
        if (argc < 2)
            throw runtime_error("too few parameters");
        string strMethod = argv[1];

        // Parameters default to strings
        Array params;
        for (int i = 2; i < argc; i++)
            params.push_back(argv[i]);
        int n = params.size();

        //
        // Special case non-string parameter types
        //
        if (strMethod == "setgenerate"            && n > 0) ConvertTo<bool>(params[0]);
        if (strMethod == "setgenerate"            && n > 1) ConvertTo<boost::int64_t>(params[1]);
        if (strMethod == "sendtoaddress"          && n > 1) ConvertTo<double>(params[1]);
        if (strMethod == "settxfee"               && n > 0) ConvertTo<double>(params[0]);
        if (strMethod == "getamountreceived"      && n > 1) ConvertTo<boost::int64_t>(params[1]); // deprecated
        if (strMethod == "getreceivedbyaddress"   && n > 1) ConvertTo<boost::int64_t>(params[1]);
        if (strMethod == "getreceivedbyaccount"   && n > 1) ConvertTo<boost::int64_t>(params[1]);
        if (strMethod == "getreceivedbylabel"     && n > 1) ConvertTo<boost::int64_t>(params[1]); // deprecated
        if (strMethod == "getallreceived"         && n > 0) ConvertTo<boost::int64_t>(params[0]); // deprecated
        if (strMethod == "getallreceived"         && n > 1) ConvertTo<bool>(params[1]); // deprecated
        if (strMethod == "listreceivedbyaddress"  && n > 0) ConvertTo<boost::int64_t>(params[0]);
        if (strMethod == "listreceivedbyaddress"  && n > 1) ConvertTo<bool>(params[1]);
        if (strMethod == "listreceivedbyaccount"  && n > 0) ConvertTo<boost::int64_t>(params[0]);
        if (strMethod == "listreceivedbyaccount"  && n > 1) ConvertTo<bool>(params[1]);
        if (strMethod == "listreceivedbylabel"    && n > 0) ConvertTo<boost::int64_t>(params[0]); // deprecated
        if (strMethod == "listreceivedbylabel"    && n > 1) ConvertTo<bool>(params[1]); // deprecated
        if (strMethod == "getbalance"             && n > 1) ConvertTo<boost::int64_t>(params[1]);
        if (strMethod == "move"                   && n > 2) ConvertTo<double>(params[2]);
        if (strMethod == "move"                   && n > 3) ConvertTo<boost::int64_t>(params[3]);
        if (strMethod == "sendfrom"               && n > 2) ConvertTo<double>(params[2]);
        if (strMethod == "sendfrom"               && n > 3) ConvertTo<boost::int64_t>(params[3]);
        if (strMethod == "listtransactions"       && n > 1) ConvertTo<boost::int64_t>(params[1]);
        if (strMethod == "listtransactions"       && n > 2) ConvertTo<boost::int64_t>(params[2]);
        if (strMethod == "listaccounts"           && n > 0) ConvertTo<boost::int64_t>(params[0]);
        if (strMethod == "walletpassphrase"       && n > 1) ConvertTo<boost::int64_t>(params[1]);
        if (strMethod == "sendmany"               && n > 1)
        {
            string s = params[1].get_str();
            Value v;
            if (!read_string(s, v) || v.type() != obj_type)
                throw runtime_error("type mismatch");
            params[1] = v.get_obj();
        }
        if (strMethod == "sendmany"                && n > 2) ConvertTo<boost::int64_t>(params[2]);

        // Execute
        Object reply = CallRPC(strMethod, params);

        // Parse reply
        const Value& result = find_value(reply, "result");
        const Value& error  = find_value(reply, "error");

        if (error.type() != null_type)
        {
            // Error
            strPrint = "error: " + write_string(error, false);
            int code = find_value(error.get_obj(), "code").get_int();
            nRet = abs(code);
        }
        else
        {
            // Result
            if (result.type() == null_type)
                strPrint = "";
            else if (result.type() == str_type)
                strPrint = result.get_str();
            else
                strPrint = write_string(result, true);
        }
    }
    catch (std::exception& e)
    {
        strPrint = string("error: ") + e.what();
        nRet = 87;
    }
    catch (...)
    {
        PrintException(NULL, "CommandLineRPC()");
    }

    if (strPrint != "")
    {
#if defined(__WXMSW__) && defined(GUI)
        // Windows GUI apps can't print to command line,
        // so settle for a message box yuck
        MyMessageBox(strPrint, "Bitcoin", wxOK);
#else
        fprintf((nRet == 0 ? stdout : stderr), "%s\n", strPrint.c_str());
#endif
    }
    return nRet;
}




#ifdef TEST
int main(int argc, char *argv[])
{
#ifdef _MSC_VER
    // Turn off microsoft heap dump noise
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, CreateFile("NUL", GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, 0));
#endif
    setbuf(stdin, NULL);
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    try
    {
        if (argc >= 2 && string(argv[1]) == "-server")
        {
            printf("server ready\n");
            ThreadRPCServer(NULL);
        }
        else
        {
            return CommandLineRPC(argc, argv);
        }
    }
    catch (std::exception& e) {
        PrintException(&e, "main()");
    } catch (...) {
        PrintException(NULL, "main()");
    }
    return 0;
}
#endif