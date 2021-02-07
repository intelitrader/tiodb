#include "../../madrinha/madrinha.h"

#include <string>
#include <vector>

#include "gtest/gtest.h"

class Madrinha : public ::testing::Test {
       protected:
	Madrinha() {
		m_master = "localhost:5000";
		m_slave_one = "localhost:4000";
		m_slave_two = "3000";
		m_slave_tree = "126.0.0.1:3000";
	};

	~Madrinha() override{};

	void SetUp() override{};

	void TearDown() override{};

	std::string m_master;
	std::string m_slave_one;
	std::string m_slave_two;
	std::string m_slave_tree;
};

TEST_F(Madrinha, ParserArguments) {
	const char* argc[] = {"unittest",	   "-m",
			      m_master.c_str(),	   "-s",
			      m_slave_one.c_str(), m_slave_two.c_str(),
			      m_slave_tree.c_str()};
	madrinha::config config;
	auto ret = madrinha::parser_arguments(std::size(argc), argc, config);
	EXPECT_EQ(ret, 0);
	EXPECT_EQ(config.master.host, "localhost");
	EXPECT_EQ(config.master.port, 5000);
	EXPECT_EQ(config.slaves[0].host, "localhost");
	EXPECT_EQ(config.slaves[0].port, 4000);
	EXPECT_EQ(config.slaves[1].host, "127.0.0.1");
	EXPECT_EQ(config.slaves[1].port, 3000);
	EXPECT_EQ(config.slaves[2].host, "126.0.0.1");
	EXPECT_EQ(config.slaves[2].port, 3000);
}

TEST_F(Madrinha, ParserInvalidPort) {
	const char* argc[] = {"unittest",	   "-m",
			      "230.0.0.1:600000",  "-s",
			      m_slave_one.c_str(), m_slave_two.c_str(),
			      m_slave_tree.c_str()};
	madrinha::config config;
	auto ret = madrinha::parser_arguments(std::size(argc), argc, config);
	EXPECT_EQ(ret, 1);
	EXPECT_EQ(config.slaves.size(), 0);
	EXPECT_EQ(config.master.host, "");
	EXPECT_EQ(config.master.port, 0);
}

TEST_F(Madrinha, ArgMissing) {
	const char* argc[] = {"unittest",	   
			      "-s",
			      m_slave_one.c_str(), m_slave_two.c_str(),
			      m_slave_tree.c_str()};
	madrinha::config config;
	auto ret = madrinha::parser_arguments(std::size(argc), argc, config);
	EXPECT_EQ(ret, 1);
	EXPECT_EQ(config.slaves.size(), 0);
	EXPECT_EQ(config.master.host, "");
	EXPECT_EQ(config.master.port, 0);
}
