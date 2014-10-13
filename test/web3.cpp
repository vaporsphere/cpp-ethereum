/*
 This file is part of cpp-ethereum.
 
 cpp-ethereum is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 
 cpp-ethereum is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
 */
/** @file web3.cpp
 * @author Alex Leverington <nessence@gmail.com>
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 * Web3 lifecycle, ethereum interface, rpc
 *
 * @todo [ethereum rpc] test hashFromNumber, blockInfo, blockDetails, transaction, unccle
 */

#include <boost/test/unit_test.hpp>

#include <libdevnet/NetMsg.h>
#include <libdevnet/NetConnection.h>
#include <libdevnet/NetEndpoint.h>
#include <libp2p/Host.h>
#include <libethereum/Client.h>

#include <libdevcrypto/FileSystem.h>
#include <libwebthree/WebThree.h>
#include "rpcservice.h"
#include "rpcprotocol.h"
#include <liblll/Compiler.h>

#include "TestHelper.h"

using namespace std;
using namespace dev;

BOOST_AUTO_TEST_SUITE( webthree )

KeyPair testKeyPair() {
	return KeyPair(Secret(fromHex("9e4c7297c67a9e17f2cea38634baecb27e05805b51931a66765c5327968e1f47")));
}

void fundTestKeyPair(WebThreeDirect &_w3) {
	// TODO: could take ptr to Client instead of webthree
	
	// Raw transaction sending eth to address of testKeyPair
	// (see testAmount in test_webthree_transactions_api)
	std::string txHex = "f87e808609184e72a00082271094db9bf2fce9595cb63ebe8458a43e6f6f09172dd7999f4f2726179a224501d762422c946590d91000000000000000801ba001e8ca3d264dd1a701cdf0f0f88e8cf67d694a64e1d83bcb880d809801462f3fa078fc4002d7587828b4f6c90f2fa8ecbe1efe39933c77eae7bd630bdfced26456";
	
	bytes txBytes(fromHex(txHex));
	_w3.ethereum()->inject(&txBytes);
	_w3.ethereum()->flushTransactions();
	eth::mine(*_w3.ethereum(), 1);
}
							  

BOOST_AUTO_TEST_CASE(test_webthree_watches_statediff_mining)
{
	/// SETUP
	WebThreeDirect direct(string("Test/v") + dev::Version + "/" DEV_QUOTED(ETH_BUILD_TYPE) "/" DEV_QUOTED(ETH_BUILD_PLATFORM), getDataDir() + "/Test", true, {"eth", "shh"});
	WebThree client;
	KeyPair k1 = testKeyPair();

	// test that client-installed watch works for client-preformed operation
	unsigned watchid = client.ethereum()->installWatch(eth::MessageFilter().to(k1.address()));
	// watches begin w/changes = 1
	assert(client.ethereum()->peekWatch(watchid) == true);
	// reset watch
	assert(client.ethereum()->checkWatch(watchid) == true);
	// should be no changes
	assert(client.ethereum()->peekWatch(watchid) == false);
	
	fundTestKeyPair(direct);

	// watch changes should be +1
	assert(client.ethereum()->peekWatch(watchid));
	
	// watch change should be in past messages
	eth::PastMessages chgs = client.ethereum()->messages(watchid);
	eth::PastMessage p = chgs[0];
	assert(p.to == k1.address());

	// recheck messages, via filter instead of watchid
	eth::PastMessages fchgs = client.ethereum()->messages(eth::MessageFilter().to(k1.address()));
	eth::PastMessage fp = chgs[0];
	assert(fp.to == k1.address());
	
	// uninstall watch
	client.ethereum()->uninstallWatch(watchid);
	assert(client.ethereum()->peekWatch(watchid) == false);
	
	// Test diff() [block]
	u256 block = fp.block;
	eth::StateDiff sd = client.ethereum()->diff(0, block);
	eth::AccountDiff ad = sd.accounts[k1.address()];
	assert(ad.balance.to().str() == "1000000000000000000000000000000000000000000000000000000000000");
	
	// Create a pending tx
	KeyPair k2 = eth::sha3("Alex's Address");
	client.ethereum()->transact(k1.secret(), 10000 , k2.address());
	client.ethereum()->flushTransactions();
	
	// Test addresses (default: m_preMine)
	Addresses ads = client.ethereum()->addresses();
	for (auto a: ads)
		assert(a != k2.address());
	
	// Test diff() [pending]
	sd = client.ethereum()->diff(0);
	ad = sd.accounts[k2.address()];
	assert(ad.balance.to() == 10000);
	
	// Test pending
	eth::Transactions pending = client.ethereum()->pending();
	assert(pending[0].sender() == k1.address());
	
	// Test addresses (pending)
	ads = client.ethereum()->addresses(0); // pending
	bool found = false;
	for (auto a: ads)
		if (a == k2.address())
			found = true;
	assert(found);
	
	// Test mining
	assert(client.ethereum()->gasLimitRemaining() > 5);
	assert(client.ethereum()->address() == Address());
	
	client.ethereum()->setAddress(k1.address());
	assert(direct.ethereum()->address() == k1.address());
	assert(client.ethereum()->address() == k1.address());
	
	assert(!client.ethereum()->isMining());
	client.ethereum()->startMining();
	
	sleep(1);
	eth::MineProgress cmp = client.ethereum()->miningProgress();
	eth::MineProgress dmp = direct.ethereum()->miningProgress();
	assert((int)cmp.current == (int)dmp.current && cmp.current > 0);
	
	assert(client.ethereum()->miningThreads());
	assert(client.ethereum()->isMining());
	client.ethereum()->stopMining();
	assert(!client.ethereum()->isMining());
	
	client.ethereum()->setMiningThreads(2);
	assert(client.ethereum()->miningThreads() == 2);
}

BOOST_AUTO_TEST_CASE(test_webthree_transactions)
{
	WebThreeDirect direct(string("Test/v") + dev::Version + "/" DEV_QUOTED(ETH_BUILD_TYPE) "/" DEV_QUOTED(ETH_BUILD_PLATFORM), getDataDir() + "/Test", true, {"eth", "shh"});
	
	Address orgAddr(fromHex("1a26338f0d905e295fccb71fa9ea849ffa12aaf4"));
	u256 directBalance = direct.ethereum()->balanceAt(orgAddr);
	assert(directBalance.str() == "1606938044258990275541962092341162602522202993782792835301376");
	
	WebThree client;
	
	/// pendingTransactions (empty)
	eth::Transactions pendingEmpty = client.ethereum()->pending();
	assert(!pendingEmpty.size());
	
	/// Test balanceAt (this balance will be reduced by tests)
	u256 clientBalance = client.ethereum()->balanceAt(orgAddr);
	clientBalance = client.ethereum()->balanceAt(orgAddr);
	assert(clientBalance == directBalance);
	
	// Raw transaction sending eth from orgAddr to address of k1 (below)
	std::string txHex = "f87e808609184e72a00082271094db9bf2fce9595cb63ebe8458a43e6f6f09172dd7999f4f2726179a224501d762422c946590d91000000000000000801ba001e8ca3d264dd1a701cdf0f0f88e8cf67d694a64e1d83bcb880d809801462f3fa078fc4002d7587828b4f6c90f2fa8ecbe1efe39933c77eae7bd630bdfced26456";
	
	// Test keypair
	// NOTE: This keypair address is the destination of raw txHex transaction.
	KeyPair k1 = testKeyPair();
	
	// Amount that is sent within raw transaction
	u256 ueth = ((((u256(1000000000) * 1000000000) * 1000000000) * 1000000000) * 1000000000) * 1000000000;
	u256 testAmount = ueth * 1000000;
	
	// Test countAt for addresses
	assert(!client.ethereum()->countAt(orgAddr));

	fundTestKeyPair(direct);
	
	assert(client.ethereum()->number());

	// pending should be empty
	assert(!client.ethereum()->pending().size());

	// todo: block parameter of balanceAt/countAt
	// orgAddr count goes up
	assert(client.ethereum()->countAt(orgAddr));
	
	// check new balances
	u256 expectSrcBal = directBalance - testAmount;
	u256 expectDstBal = testAmount;

	u256 newSrcBal = client.ethereum()->balanceAt(orgAddr);
	u256 k1Bal = client.ethereum()->balanceAt(k1.address());
	cout << "New source balance: " << newSrcBal;
	cout << "Expected source balance: " << expectDstBal.str();
	
	// transaction uses gas so srcBal will be more/less than supplied gas (2x for contract+valuetx)
	assert(client.ethereum()->balanceAt(orgAddr) < expectSrcBal);
	assert(client.ethereum()->balanceAt(orgAddr) >= expectSrcBal - 2*10000*10*eth::szabo);
	assert(client.ethereum()->balanceAt(k1.address()) == expectDstBal);
	
	// New address to test w/transact() (change destination to source, new dest)
	KeyPair k2 = KeyPair::create();

	/// Test transact (transfer ether)
	u256 test2Amount = testAmount / 2;
	client.ethereum()->transact(k1.secret(), test2Amount , k2.address());;
	client.ethereum()->flushTransactions();
	eth::mine(*direct.ethereum(), 1);
	assert(client.ethereum()->number() == 2);

	u256 expectedK1Bal = k1Bal - test2Amount;
	
	assert(client.ethereum()->balanceAt(k1.address()) < expectedK1Bal);
	assert(client.ethereum()->balanceAt(k1.address()) >= expectedK1Bal - 10000*10*eth::szabo);
	assert(client.ethereum()->balanceAt(k2.address()) == test2Amount);
	
	assert(client.ethereum()->countAt(k1.address()));

	// Test create contract
	// compile contract
	std::string kvContract("{[[69]] (caller) (return 0 (lll (when (= (caller) @@69) (for {} (< @i (calldatasize)) [i](+ @i 64) [[ (calldataload @i) ]] (calldataload (+ @i 32)) ) ) 0))}");
	
	vector<string> lllErrors;
	bytes contractBytes(eth::compileLLL(kvContract, true, &lllErrors));
	cout << "contract bytes: " << toHex(contractBytes);
	// 33604557602a8060106000396000f200604556330e0f602a59366080530a0f602a59602060805301356080533557604060805301608054600958
	assert(!lllErrors.size());
	
	Address contract(client.ethereum()->transact(k1.secret(), 10000, contractBytes));
	cout << "Contract Address: " << toString(contract) << endl;
//	assert(toString(contract) == "a9ef99de99567ed0627358a53ac8f771e3301cdf");
	
	client.ethereum()->flushTransactions();
	eth::mine(*direct.ethereum(), 1);

	// Test call (broken?)
//	bytes directCall = direct.ethereum()->call(k1.secret(), 0, contract, asBytes("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAtest"));
//	bytes clientCall = client.ethereum()->call(k1.secret(), 0, contract, asBytes("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAtest"));
//	assert(directCall == clientCall);
	
	// Test codeAt (for now, just test that something is returned)
	bytes codeAtBytes = client.ethereum()->codeAt(contract);
	assert(codeAtBytes.size());
	cout << "codeAt bytes: " << toHex(codeAtBytes);
	
	// Test stateAt
	u256 state(client.ethereum()->stateAt(contract, 69));
	cout << "stateAt: " << toString(Address(right160(state)));
	assert(Address(right160(state)) == k1.address());
	
	// Test transact(); publish ASCII key-value pair +mine
	client.ethereum()->transact(k1.secret(), 0, contract, asBytes("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAtest"));
	client.ethereum()->flushTransactions();
	eth::mine(*direct.ethereum(), 1);

	// Test stateAt
	u256 at = (h256)asBytes("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
	u256 postTestState(client.ethereum()->stateAt(contract, at));
	clog(RPCNote) << "postTestState: " << (h256)postTestState;
	std::string sb((char*)((h256)postTestState).asBytes().data());
	assert(sb == "test");
	
	// Test storageAt
	std::map<u256, u256> storage(client.ethereum()->storageAt(contract));
	u256 callerState = storage[69];
	h256 testState = storage[at];
	
	assert(Address(right160(callerState)) == k1.address());
	assert(std::string((char*)testState.data()) == std::string("test"));
	
	
}

BOOST_AUTO_TEST_SUITE_END()


BOOST_AUTO_TEST_SUITE( netproto )

BOOST_AUTO_TEST_CASE(test_netproto_simple)
{
	return;
	cout << "test_netproto_simple" << endl;
	
	// Shared
	boost::asio::ip::tcp::endpoint ep(boost::asio::ip::address::from_string("127.0.0.1"), 30310);
	
	
	/// Service<Protocol> && Protocol<Service>
	TestService s(nullptr);
	TestServiceProtocol p((NetConnection *)nullptr, &s);
	assert(TestServiceProtocol::serviceId() == 255);
	assert(s.serviceId() == TestServiceProtocol::serviceId());
	
	// sA: To be used by client request test...
	std::string sA = s.serviceString();
	assert(sA == "serviceString");
	
	// pA: To be used by server request test...
	std::string pA = p.protocolString();
	assert(pA == "protocolString");

	// Access interface through service (used by protocol)
	assert(s.m_interface->stringTest() == "string");

	// Client request test:
	
	// Register service, start endpoint
	shared_ptr<NetEndpoint> netEp(new NetEndpoint(ep));
	netEp->registerService(&s);
	netEp->start();
	
	// Client connection
	auto clientConn = make_shared<NetConnection>(netEp->get_io_service(), ep);
	TestRPCProtocol clientProtocol(clientConn.get());
	clientConn->start();
	
	// wait for handshake
	// todo: this may be removable as requests are queued
	while (!clientConn->connectionOpen() && !clientConn->connectionError());
	
	
	
}

BOOST_AUTO_TEST_CASE(test_netendpoint)
{
	return;
	boost::asio::ip::tcp::endpoint ep(boost::asio::ip::address::from_string("127.0.0.1"), 30310);

	shared_ptr<NetEndpoint> netEp(new NetEndpoint(ep));
	netEp->start();
	
	auto testConn = make_shared<NetConnection>(netEp->get_io_service(), ep);
	testConn->start();
	
	while (!testConn->connectionOpen() && !testConn->connectionError());
	
	netEp->stop();
	testConn.reset();
	
	netEp->start();
	testConn.reset(new NetConnection(netEp->get_io_service(), ep));
	testConn->start();
	while (!testConn->connectionOpen() && !testConn->connectionError());
	testConn.reset();
	netEp->stop();
	netEp.reset();
	testConn.reset();
}
	
BOOST_AUTO_TEST_CASE(test_netservice)
{
	return;
	boost::asio::ip::tcp::endpoint ep(boost::asio::ip::address::from_string("127.0.0.1"), 30310);

	p2p::Host net("test", p2p::NetworkPreferences());
	unique_ptr<eth::Client> eth(new eth::Client(&net));
	clog(RPCNote) << "Blockchain opened. Starting RPC Server.";
	
	unique_ptr<EthereumRPC> server(new EthereumRPC(eth.get()));
	
	shared_ptr<NetEndpoint> netEp(new NetEndpoint(ep));
	netEp->registerService(server.get());
	netEp->start();
	
	// rpc client requries connection and server
	
	auto clientConn = make_shared<NetConnection>(netEp->get_io_service(), ep);
	EthereumRPCClient client(clientConn.get());
	clientConn->start();
	
	Address a(fromHex("1a26338f0d905e295fccb71fa9ea849ffa12aaf4"));
	u256 balance = client.balanceAt(a, -1);
	cout << "Got balanceAt: " << balance << endl;

	assert(balance.str() == "1606938044258990275541962092341162602522202993782792835301376");
}
	
BOOST_AUTO_TEST_SUITE_END()

	
BOOST_AUTO_TEST_SUITE( rlpnet )

BOOST_AUTO_TEST_CASE(test_rlpnet_messages)
{
	// service message:
	RLPStream s;
	s.appendList(1);
	s << "version";
	
	NetMsg version((NetMsgServiceType)0, 0, (NetMsgType)0, RLP(s.out()));
	
	bytes rlpbytes = version.payload();
	RLP rlp = RLP(bytesConstRef(&rlpbytes).cropped(4));
	assert("version" == rlp[2][0].toString());
	bytes vb = rlp[2][0].data().toBytes();
	assert("version" == RLP(vb).toString());
	assert("version" == RLP(version.rlp())[0].toString());
	
	// application message:
	NetMsg appthing((NetMsgServiceType)1, 0, (NetMsgType)1, RLP(s.out()));
	
	rlpbytes = appthing.payload();
	rlp = RLP(bytesConstRef(&rlpbytes).cropped(4));
	
	assert("version" == rlp[3].toString());
	
	// converting to const storage (this fails)
	bytes rlpb = rlp[3].data().toBytes();
	RLP rlpFromBytes = RLP(rlpb);
	assert("version" == rlpFromBytes.toString());

}

BOOST_AUTO_TEST_CASE(test_rlpnet_connectionin)
{
	cout << "test_rlpnet_connectionin" << endl;
	
	boost::asio::io_service io;
	boost::asio::ip::tcp::endpoint ep(boost::asio::ip::address::from_string("127.0.0.1"), 30310);
	NetConnection* n = new NetConnection(io, ep);
	assert(!n->connectionOpen());
	delete n;
	
	auto sharedconn = make_shared<NetConnection>(io);
	sharedconn.reset();;
	
	auto testNoConnection = make_shared<NetConnection>(io, ep);
	testNoConnection->start();
	io.run();
	testNoConnection.reset();
	io.reset();
}

void acceptConnection(boost::asio::ip::tcp::acceptor& _a, boost::asio::io_service &acceptIo, boost::asio::ip::tcp::endpoint ep, std::atomic<size_t>* connected)
{
	// Acceptor is to be replaced with Net implementation.
//	static std::vector<shared_ptr<NetConnection> > conns;
//	
//	auto newConn = make_shared<NetConnection>(acceptIo);
//	conns.push_back(newConn);
//	_a.async_accept(*newConn->socket(), [newConn, &_a, &acceptIo, ep, connected](boost::system::error_code _ec)
//	{
//		acceptConnection(_a, acceptIo, ep, connected);
//		connected->fetch_add(1);
//		
//		if (!_ec)
//			newConn->start();
//		else
//			clog(RPCNote) << "error accepting socket: " << _ec.message();
//	});
};

BOOST_AUTO_TEST_CASE(test_rlpnet_connections)
{
	
	// Disabled: Acceptor is to be replaced with Net implementation.
	return;
	cout << "test_rlpnet_rapid_connections" << endl;
	
	// Shared:
	boost::asio::ip::tcp::endpoint ep(boost::asio::ip::address::from_string("127.0.0.1"), 30310);
	std::atomic<bool> stopThread(false);
	
	// Client IO:
	boost::asio::io_service io;
	std::vector<shared_ptr<NetConnection> > conns;
	
	// Acceptor IO, Thread:
	std::atomic<size_t> acceptedConnections(0);
	boost::asio::io_service acceptorIo;
	std::thread acceptorIoThread([&]()
	{
		boost::asio::ip::tcp::acceptor acceptor(acceptorIo, ep);
		acceptConnection(acceptor, acceptorIo, ep, &acceptedConnections);
		
		// Continue to run when out of work
		while (!stopThread)
		{
			clog(RPCNote) << "acceptorIoThread started";
			
			if (acceptorIo.stopped())
				acceptorIo.reset();
			acceptorIo.run();
			usleep(1000);
		}
		clog(RPCNote) << "acceptorIoThread stopped";
	});
	
	
	// Client Connections:
	
	// create first connection so IO has work
	auto connOut = make_shared<NetConnection>(acceptorIo, ep);
	connOut->start();
	conns.push_back(connOut);
	
//	std::thread ioThread([&]()
//	{
//		while (!stopThread)
//		{
//			clog(RPCNote) << "ioThread started";
//			
//			if (io.stopped())
//				io.reset();
//			io.run();
//			usleep(1000);
//		}
//		
//		clog(RPCNote) << "ioThread stopped";
//	});

	for (auto i = 0; i < 256; i++)
	{
		usleep(2000);
		auto connOut = make_shared<NetConnection>(acceptorIo, ep);
		connOut->start();
		conns.push_back(connOut);
	}

	std::set<NetConnection*> connErrs;
	while (acceptedConnections + connErrs.size() != conns.size())
	{
		for (auto c: conns)
			if (c->connectionError())
				connErrs.insert(c.get());
		
		usleep(250);
	}
	assert(acceptedConnections + connErrs.size() == conns.size());
	
	// tailend connections likely won't even start
	for (auto c: conns)
	{
		// wait for connection to open or error
		while (!c->connectionOpen() && !c->connectionError());
		
		// shutdown
		c->shutdown();
		
		// wait until shutdown
		while (c->connectionOpen() && !c->connectionError());
		
		if (c->connectionError())
			connErrs.insert(c.get());
	}

	
	// Before shutting down IO service, all connections need to be deleted
	// so connections are deallocate prior to IO service deallocation
	for (auto c: conns)
		c.reset();
	connOut.reset();
	
	stopThread.store(true);

	acceptorIo.stop();
	acceptorIoThread.join();
	
	io.stop();
//	ioThread.join();

	clog(RPCNote) << "Connections: " << acceptedConnections << "Failed: " << connErrs.size();
	
}
	
BOOST_AUTO_TEST_SUITE_END()

