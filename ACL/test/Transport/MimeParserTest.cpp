/*
 * MimeParserTest.cpp
 *
 * Copyright 2017 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 *     http://aws.amazon.com/apache2.0/
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

/// @file MimeParserTest.cpp

#include <memory>
#include <random>

#include <gtest/gtest.h>

#include <curl/curl.h>
#include <ACL/Transport/HTTP2Stream.h>
#include "ACL/Transport/MessageConsumerInterface.h"
#include <AVSCommon/SDKInterfaces/MessageObserverInterface.h>

#include "Common/TestableAttachmentManager.h"

#include "TestableConsumer.h"
#include "MockMessageRequest.h"
#include "Common/Common.h"
#include "Common/MimeUtils.h"
#include "Common/TestableMessageObserver.h"

namespace alexaClientSDK {
namespace acl {
namespace test {

using namespace avsCommon::sdkInterfaces;
using namespace avsCommon::avs::attachment;

/// The size of the data for directive and attachments we will use.
static const int TEST_DATA_SIZE = 100;
/// The number of segments that the MIME string will be broken into during simple testing.
static const int TEST_MULTI_WRITE_ITERATIONS = 4;
/// An upper bound that the feedParser logic may use to ensure we don't loop infinitely.
static const int TEST_MULTI_MAX_ITERATIONS = 100;
/// A test context id.
static const std::string TEST_CONTEXT_ID = "TEST_CONTEXT_ID";
/// A test content id.
static const std::string TEST_CONTENT_ID_01 = "TEST_CONTENT_ID_01";
/// A test content id.
static const std::string TEST_CONTENT_ID_02 = "TEST_CONTENT_ID_02";
/// A test content id.
static const std::string TEST_CONTENT_ID_03 = "TEST_CONTENT_ID_03";
/// A test boundary string, copied from a real interaction with AVS.
static const std::string MIME_TEST_BOUNDARY_STRING = "84109348-943b-4446-85e6-e73eda9fac43";

/**
 * Our GTest class.
 */
class MimeParserTest : public ::testing::Test {
public:
    /**
     * Construct the objects we will use across tests.
     */
    void SetUp() override {
        m_attachmentManager = std::make_shared<TestableAttachmentManager>();

        m_testableMessageObserver = std::make_shared<TestableMessageObserver>();
        m_testableConsumer = std::make_shared<TestableConsumer>();
        m_testableConsumer->setMessageObserver(m_testableMessageObserver);

        m_parser = std::make_shared<MimeParser>(m_testableConsumer, m_attachmentManager);
        m_parser->setAttachmentContextId(TEST_CONTEXT_ID);
        m_parser->setBoundaryString(MIME_TEST_BOUNDARY_STRING);
    }

    /**
     * A utility function to feed data into our MimeParser object.  A result of this function is that the MimeParser
     * object will route Directives and Attachments to the appropriate objects as they are broken out of the
     * aggregate MIME string.
     *
     * @param data The MIME string to be parsed.
     * @param numberIterations The number of segments the MIME string is to be broken into, and then fed to the parser.
     */
    void feedParser(const std::string& data, int numberIterations = 1) {
        // Here we're simulating an ACL stream.  We've got a mime string that we will feed to the mime parser in chunks.
        // If any chunk fails (due to simulated attachment failing to write), we will re-drive it.

        int writeQuantum = data.length();
        if (numberIterations > 1) {
            writeQuantum /= numberIterations;
        }

        size_t numberBytesWritten = 0;
        int iterations = 0;
        while (numberBytesWritten < data.length() && iterations < TEST_MULTI_MAX_ITERATIONS) {
            int bytesRemaining = data.length() - numberBytesWritten;
            int bytesToFeed = bytesRemaining < writeQuantum ? bytesRemaining : writeQuantum;

            if (MimeParser::DataParsedStatus::OK ==
                m_parser->feed(const_cast<char*>(&(data.c_str()[numberBytesWritten])), bytesToFeed)) {
                numberBytesWritten += bytesToFeed;
            }

            iterations++;
        }
    }

    /**
     * A utility function to validate that each MimePart we're tracking was received ok at its expected destination.
     */
    void validateMimePartsParsedOk() {
        for (auto mimePart : m_mimeParts) {
            ASSERT_TRUE(mimePart->validateMimeParsing());
        }
    }

    /// Our MimePart vector.
    std::vector<std::shared_ptr<TestMimePart>> m_mimeParts;
    /// The AttachmentManager.
    std::shared_ptr<TestableAttachmentManager> m_attachmentManager;
    /// The ACL consumer object which the MimeParser requires.
    std::shared_ptr<TestableConsumer> m_testableConsumer;
    /// An observer which will receive Directives.
    std::shared_ptr<TestableMessageObserver> m_testableMessageObserver;
    /// The MimeParser which we will be primarily testing.
    std::shared_ptr<MimeParser> m_parser;
};

/**
 * Test feeding a MIME string to the parser in a single pass which only contains a JSON message.
 */
TEST_F(MimeParserTest, testDirectiveReceivedSingleWrite) {
    m_mimeParts.push_back(std::make_shared<TestMimeJsonPart>(TEST_DATA_SIZE, m_testableMessageObserver));

    auto mimeString = constructTestMimeString(m_mimeParts, MIME_TEST_BOUNDARY_STRING);
    feedParser(mimeString);

    validateMimePartsParsedOk();
}

/**
 * Test feeding a MIME string to the parser in multiple passes which only contains a JSON message.
 */
TEST_F(MimeParserTest, testDirectiveReceivedMultiWrite) {
    m_mimeParts.push_back(std::make_shared<TestMimeJsonPart>(TEST_DATA_SIZE, m_testableMessageObserver));

    auto mimeString = constructTestMimeString(m_mimeParts, MIME_TEST_BOUNDARY_STRING);
    feedParser(mimeString, TEST_MULTI_WRITE_ITERATIONS);

    validateMimePartsParsedOk();
}

/**
 * Test feeding a MIME string to the parser in a single pass which only contains a binary attachment message.
 */
TEST_F(MimeParserTest, testAttachmentReceivedSingleWrite) {
    m_mimeParts.push_back(std::make_shared<TestMimeAttachmentPart>(
        TEST_CONTEXT_ID, TEST_CONTENT_ID_01, TEST_DATA_SIZE, m_attachmentManager));

    auto mimeString = constructTestMimeString(m_mimeParts, MIME_TEST_BOUNDARY_STRING);
    feedParser(mimeString);

    validateMimePartsParsedOk();
}

/**
 * Test feeding a MIME string to the parser in multiple passes which only contains a binary attachment message.
 */
TEST_F(MimeParserTest, testAttachmentReceivedMultiWrite) {
    m_mimeParts.push_back(std::make_shared<TestMimeAttachmentPart>(
        TEST_CONTEXT_ID, TEST_CONTENT_ID_01, TEST_DATA_SIZE, m_attachmentManager));

    auto mimeString = constructTestMimeString(m_mimeParts, MIME_TEST_BOUNDARY_STRING);
    feedParser(mimeString, TEST_MULTI_WRITE_ITERATIONS);

    validateMimePartsParsedOk();
}

/**
 * Test feeding a MIME string to the parser in a single pass which contains a JSON message followed by
 * a binary attachment message.
 */
TEST_F(MimeParserTest, testDirectiveAndAttachmentReceivedSingleWrite) {
    m_mimeParts.push_back(std::make_shared<TestMimeJsonPart>(TEST_DATA_SIZE, m_testableMessageObserver));
    m_mimeParts.push_back(std::make_shared<TestMimeAttachmentPart>(
        TEST_CONTEXT_ID, TEST_CONTENT_ID_01, TEST_DATA_SIZE, m_attachmentManager));

    auto mimeString = constructTestMimeString(m_mimeParts, MIME_TEST_BOUNDARY_STRING);
    feedParser(mimeString);

    validateMimePartsParsedOk();
}

/**
 * Test feeding a MIME string to the parser in multiple passes which contains a JSON message followed by
 * a binary attachment message.
 */
TEST_F(MimeParserTest, testDirectiveAndAttachmentReceivedMultiWrite) {
    m_mimeParts.push_back(std::make_shared<TestMimeJsonPart>(TEST_DATA_SIZE, m_testableMessageObserver));
    m_mimeParts.push_back(std::make_shared<TestMimeAttachmentPart>(
        TEST_CONTEXT_ID, TEST_CONTENT_ID_01, TEST_DATA_SIZE, m_attachmentManager));

    auto mimeString = constructTestMimeString(m_mimeParts, MIME_TEST_BOUNDARY_STRING);
    feedParser(mimeString, TEST_MULTI_WRITE_ITERATIONS);

    validateMimePartsParsedOk();
}

}  // namespace test
}  // namespace acl
}  // namespace alexaClientSDK

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
