#ifdef __linux__
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#else
typedef __int32 int32_t;
typedef unsigned __int32 uint32_t;
typedef __int64 int64_t;
typedef unsigned __int64 uint64_t;
#define PRIu64       "I64u"
#define PRIx64       "I64x"
#endif

#include "CppUTest/TestHarness.h"

extern "C"
{
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "mrp_doubles.h"
#include "mrp.h"
#include "msrp.h"
#include "parse.h"

/* Most MSRP commands operate on the global DB */
extern struct msrp_database *MSRP_db;

void msrp_event_observer(int event, struct msrp_attribute *attr);
char *msrp_attrib_type_string(int t);
char *mrp_event_string(int e);
}

/* This include file contains all the byte buffers implementing
 * packets for the tests */
#include "sample_msrp_packets.h"

/* Needed for msrp_recv_cmd() */
static struct sockaddr_in client;

/*
 * The test_state keeps track of counts of all events that are
 * triggered during a test, and these functions show how to read
 * them.
 */

void dump_events() {
	int i, evtid, evtcount;
	evtcount = (sizeof test_state.msrp_event_counts /
		    sizeof *test_state.msrp_event_counts);
	printf("\nMSRP Event Counts:\n");
	for (i = 0; i < evtcount; i++) {
		if (test_state.msrp_event_counts[i] > 0) {
			printf("%s: %d\n", mrp_event_string(MSRP_EVENT_ID(i)),
			       test_state.msrp_event_counts[i]);
		}
	}
}

int count_events() {
	int i, total, evtcount;
	evtcount = (sizeof test_state.msrp_event_counts /
		    sizeof *test_state.msrp_event_counts);
	total = 0;
	for (i = 0; i < evtcount; i++) {
		total += test_state.msrp_event_counts[i];
	}
	return total;
}

/* A sample event observer; this is called on every event generated by
 * the msrp_recv_msg function, so each attribute parsed will be passed
 * to it in turn and can be analyzed. The following line inserted into
 * a test will enable it:
 *
 * test_state.msrp_observe = msrp_event_observer;
 */
void msrp_event_observer(int event, struct msrp_attribute *attr)
{
	printf("Event: %s Attr: %s StreamID: ",
	       mrp_event_string(event),
	       msrp_attrib_type_string(attr->type));
	printf("%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
	       attr->attribute.talk_listen.StreamID[0],
	       attr->attribute.talk_listen.StreamID[1],
	       attr->attribute.talk_listen.StreamID[2],
	       attr->attribute.talk_listen.StreamID[3],
	       attr->attribute.talk_listen.StreamID[4],
	       attr->attribute.talk_listen.StreamID[5],
	       attr->attribute.talk_listen.StreamID[6],
	       attr->attribute.talk_listen.StreamID[7]);
}

/* Test doubles for the mrpd functionality enable feeding PDU buffers
 * into the msrp code as if they were received from the network and
 * observing the resulting MSRP events.
 *
 * test_state.rx_PDU - a buffer to hold packet data for simulated Rx
 *
 * test_state.rx_PDU_len - store the length of stored PDU data here
 *
 * test_state.forward_msrp_events - when set to 0, only the test
 *    double code for observing events will run. When set to 1, the
 *    observation code will run and then the events will pass to the
 *    normal processing.
 *
 * test_state.msrp_observe - a function pointer that, if not NULL,
 *    will be called on every event that occurs during a test.
 */

/******* Start of test cases *******/

TEST_GROUP(MsrpPDUTests)
{
	void setup() {
		mrpd_reset();
		test_state.forward_msrp_events = 0;
		msrp_init(1);
	}
	void teardown() {
		msrp_reset();
		mrpd_reset();
	}
};

/* This is a sample packet captured via wireshark, not really a
 * specific test case, but many are based on it. We just test that
 * the parse function returns success */
TEST(MsrpPDUTests, ParsePkt2)
{
	int rv;
	memcpy(test_state.rx_PDU, pkt2, sizeof pkt2);
	test_state.rx_PDU_len = sizeof pkt2;
//	test_state.msrp_observe = msrp_event_observer;
	rv = msrp_recv_msg();
	LONGS_EQUAL(0, rv);
}

/*
 * This series of tests ensures that spurious events aren't generated
 * by packets that have various problems.
 */

TEST(MsrpPDUTests, NoEventsWhenDAIsWrong)
{
	int rv;
	memcpy(test_state.rx_PDU, badDA, sizeof badDA);
	test_state.rx_PDU_len = sizeof badDA;
	rv = msrp_recv_msg();
	LONGS_EQUAL(0, count_events());
	LONGS_EQUAL(-1, rv);
}

TEST(MsrpPDUTests, NoEventsWhenEthertypeIsWrong)
{
	int rv;
	memcpy(test_state.rx_PDU, badEthertype, sizeof badEthertype);
	test_state.rx_PDU_len = sizeof badEthertype;
	rv = msrp_recv_msg();
	LONGS_EQUAL(0, count_events());
	LONGS_EQUAL(-1, rv);
}

TEST(MsrpPDUTests, NoEventsWhenOnlyAttributeTypeIsUnknown)
{
	int rv;
	memcpy(test_state.rx_PDU, badAttrType, sizeof badAttrType);
	test_state.rx_PDU_len = sizeof badAttrType;
	rv = msrp_recv_msg();
	LONGS_EQUAL(0, count_events());
	LONGS_EQUAL(-1, rv);
}

TEST(MsrpPDUTests, NoEventsBecauseAttributeType0IsReserved)
{
	int rv;
	int len = sizeof badAttrType2;

	memcpy(test_state.rx_PDU, badAttrType2, len);
	test_state.rx_PDU_len = len;
	rv = msrp_recv_msg();
	LONGS_EQUAL(0, count_events());
	LONGS_EQUAL(-1, rv);
}

/*
 * Test the Ignore value of FourPackedEvents; ignored elements should
 * not generate events but their FirstValue increments should still
 * occur.
 */

TEST(MsrpPDUTests, NoEventsWhenOnlyFourPackedValueIsIgnore)
{
	int rv;
	memcpy(test_state.rx_PDU, onlyIgnore, sizeof onlyIgnore);
	test_state.rx_PDU_len = sizeof onlyIgnore;
	rv = msrp_recv_msg();
	LONGS_EQUAL(0, count_events());
	LONGS_EQUAL(0, rv);
}

void ignore_increment_observer(int event, struct msrp_attribute *attr) {
	static unsigned char lastByte = 255; /* Don't use test values
					      * where this is valid   */
	unsigned char expectedSkip = 3; /* We are skipping second and
					   third values in the 4-pack */
	unsigned char newLastByte;
	int diff;

	newLastByte = attr->attribute.talk_listen.StreamID[7];

	if (lastByte < 255) { /* Only check on every other event */
		diff = newLastByte - lastByte;
		lastByte = 255;
		LONGS_EQUAL(expectedSkip, diff);
	} else {              /* When not checking, set value    */
		lastByte = newLastByte;
	}
}

TEST(MsrpPDUTests, MiddleIgnoresStillIncrement)
{
	int rv;
	unsigned char *pkt = someIgnore;
	int len = sizeof someIgnore;

	memcpy(test_state.rx_PDU, pkt, len);
	test_state.rx_PDU_len = len;
	test_state.msrp_observe = ignore_increment_observer;
	rv = msrp_recv_msg();
	LONGS_EQUAL(2, count_events());
	LONGS_EQUAL(0, rv);
}

/*
 * AttributeLength must match known value from specification, so
 * variations are malformed even if they match the actual length of
 * the FirstValue field.
 */

TEST(MsrpPDUTests, NoEventsWhenTalkerAdvertiseAttributeLengthIsWrong)
{
	int rv;
	unsigned char *pkt = wrongTalkerAdvertiseLength;
	int len = sizeof wrongTalkerAdvertiseLength;

	memcpy(test_state.rx_PDU, pkt, len);
	test_state.rx_PDU_len = len;
	rv = msrp_recv_msg();
	LONGS_EQUAL(0, count_events());
	LONGS_EQUAL(-1, rv);
}

TEST(MsrpPDUTests, NoEventsWhenTalkerFailedAttributeLengthIsWrong)
{
	int rv;
	unsigned char *pkt = wrongTalkerFailedLength;
	int len = sizeof wrongTalkerFailedLength;

	memcpy(test_state.rx_PDU, pkt, len);
	test_state.rx_PDU_len = len;
	rv = msrp_recv_msg();
	LONGS_EQUAL(0, count_events());
	LONGS_EQUAL(-1, rv);
}

TEST(MsrpPDUTests, NoEventsWhenListenerAttributeLengthIsWrong)
{
	int rv;
	unsigned char *pkt = wrongListenerLength;
	int len = sizeof wrongListenerLength;

	memcpy(test_state.rx_PDU, pkt, len);
	test_state.rx_PDU_len = len;
	rv = msrp_recv_msg();
	LONGS_EQUAL(0, count_events());
	LONGS_EQUAL(-1, rv);
}

TEST(MsrpPDUTests, NoEventsWhenDomainAttributeLengthIsWrong)
{
	int rv;
	unsigned char *pkt = wrongDomainAttributeLength;
	int len = sizeof wrongDomainAttributeLength;

	memcpy(test_state.rx_PDU, pkt, len);
	test_state.rx_PDU_len = len;
	rv = msrp_recv_msg();
	LONGS_EQUAL(0, count_events());
	LONGS_EQUAL(-1, rv);
}

/*
 * This series of tests ensures that LeaveAll events work on their own
 * and together, and that each applies to its own attribute.
 */

/* This ensures all events are LeaveAll events in the following tests */
static void leaveall_observer(int event, struct msrp_attribute *attr) {
	(void)attr;
	LONGS_EQUAL(MRP_EVENT_RLA, event);
}

TEST(MsrpPDUTests, LeaveAllEventGeneratesLAEventForTalkerAttribute)
{
	int rv;
	memcpy(test_state.rx_PDU, laTalker, sizeof laTalker);
	test_state.rx_PDU_len = sizeof laTalker;
	test_state.msrp_observe = leaveall_observer;
	rv = msrp_recv_msg();
	LONGS_EQUAL(1, count_events());
	LONGS_EQUAL(0, rv);
}

TEST(MsrpPDUTests, LeaveAllEventGeneratesLAEventForTalkerFailAttribute)
{
	int rv;
	memcpy(test_state.rx_PDU, laTalkerFail, sizeof laTalkerFail);
	test_state.rx_PDU_len = sizeof laTalkerFail;
	test_state.msrp_observe = leaveall_observer;
	rv = msrp_recv_msg();
	LONGS_EQUAL(1, count_events());
	LONGS_EQUAL(0, rv);
}

TEST(MsrpPDUTests, LeaveAllEventGeneratesLAEventForListenerAttribute)
{
	int rv;
	memcpy(test_state.rx_PDU, laListener, sizeof laListener);
	test_state.rx_PDU_len = sizeof laListener;
	test_state.msrp_observe = leaveall_observer;
	rv = msrp_recv_msg();
	LONGS_EQUAL(1, count_events());
	LONGS_EQUAL(0, rv);
}

TEST(MsrpPDUTests, LeaveAllEventGeneratesLAEventForDomainAttribute)
{
	int rv;
	memcpy(test_state.rx_PDU, laDomain, sizeof laDomain);
	test_state.rx_PDU_len = sizeof laDomain;
	test_state.msrp_observe = leaveall_observer;
	rv = msrp_recv_msg();
	LONGS_EQUAL(1, count_events());
	LONGS_EQUAL(0, rv);
}

TEST(MsrpPDUTests, LeaveAllEventsForAllAttributes)
{
	int rv;
	memcpy(test_state.rx_PDU, leave_all, sizeof leave_all);
	test_state.rx_PDU_len = sizeof leave_all;
	test_state.msrp_observe = leaveall_observer;
	rv = msrp_recv_msg();
	LONGS_EQUAL(4, count_events());
	LONGS_EQUAL(0, rv);
}

/*
 * Tests for correct handling of abnormally ended MRPDUs.
 *
 * If the PDU ends before an EndMark is processed, processing is
 * terminated as if an EndMark had been reached. (10.8.1.2 f)
 *
 * An incomplete VectorAttribute at the end of the PDU causes the
 * entire PDU to be discarded. (10.8.3.3)
 */


/* I am not certain about the expected behavior here. I believe it
 * should discard the PDU as the overall Message is malformed when
 * there are fewer bytes in the PDU than promised by the
 * AttributeListLength (see 10.8.3.3), but on the other hand it's
 * terminating with a complete VectorAttribute and 10.8.3.4 suggests
 * such should be processed.
 *
 * Current status: No messages are generated, but it returns success?
 */
IGNORE_TEST(MsrpPDUTests, TruncatedPacketFewerBytesThanListLength)
{
	int rv;
	unsigned char *pkt = partialMsgFullAttribute;
	int len = sizeof partialMsgFullAttribute;

	memcpy(test_state.rx_PDU, pkt, len);
	test_state.rx_PDU_len = len;
	rv = msrp_recv_msg();
	LONGS_EQUAL(0, count_events());
	LONGS_EQUAL(-1, rv);
}


/* I believe that this should succeed and generate all 86 events due
 * to the instructions in 10.8.3.4, but it again returns success and
 * generates no events.
 *
 * I think it's being discarded because the AttributeListLength
 * includes the EndMark that should terminates the AttributeList, but
 * it's not in the PDU. But 10.8.1.2 point 'f' says "If the end of an
 * MRPDU is encountered before an EndMark is reached, then processing
 * of the PDU is terminated as if an EndMark had been reached." and
 * this could be interpreted to mean assuming the extra 2 bytes of
 * EndMark exist.
 */
IGNORE_TEST(MsrpPDUTests, TruncatedPacketCompleteMessageNoEndmarks)
{
	int rv;
	unsigned char *pkt = fullMsgNoEndmark;
	int len = sizeof fullMsgNoEndmark;

	memcpy(test_state.rx_PDU, pkt, len);
	test_state.rx_PDU_len = len;
	rv = msrp_recv_msg();
	LONGS_EQUAL(86, count_events());
	LONGS_EQUAL(0, rv);
}

/* This one has a single EndMark, missing the one that would normally
 * terminate the top-level Message List. This is unambiguously
 * required to work by 10.8.3.4, and it does.
 */
TEST(MsrpPDUTests, TruncatedPacketCompleteMessageOneEndmark)
{
	int rv;
	unsigned char *pkt = fullMsgOneEndmark;
	int len = sizeof fullMsgOneEndmark;

	memcpy(test_state.rx_PDU, pkt, len);
	test_state.rx_PDU_len = len;
	rv = msrp_recv_msg();
	LONGS_EQUAL(86, count_events());
	LONGS_EQUAL(0, rv);
}



/*
 * Future tests to implement that I'm fairly sure wouldn't pass now:
 *
 *    * Correctly handling higher protocol version than supported
 */
