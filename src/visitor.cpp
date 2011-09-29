/*
 *  Copyright 2011 Mikko Koppanen <mikko@kuut.io>
 *  
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *  
 *      http://www.apache.org/licenses/LICENSE-2.0
 *  
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.                 
 */
#include "visitor.hpp"
#include "time.hpp"

bool pzq::visitor_t::can_write ()
{
    int events = 0;
    size_t optsiz = sizeof (int);

    m_socket.get ()->getsockopt (ZMQ_EVENTS, &events, &optsiz);

    if (events & ZMQ_POLLOUT)
        return true;

    return false;
}

const char *pzq::visitor_t::visit_full (const char *kbuf, size_t ksiz, const char *vbuf, size_t vsiz, size_t *sp) 
{
    uint64_t pos = 0;
    size_t msg_size;


	std::string key (kbuf, ksiz);

	if (m_store.get ()->is_in_flight (key))
	{
		return NOP;
	}

    if (!can_write ())
    	throw std::runtime_error ("The socket is in blocking state");

    pzq::message_t parts;

    boost::shared_ptr<zmq::message_t> header (new zmq::message_t (ksiz));
    memcpy (header.get ()->data (), kbuf, ksiz);
    parts.push_back (header);

    // Time when the message goes out
    std::stringstream mt;
    mt << pzq::microsecond_timestamp ();

    boost::shared_ptr<zmq::message_t> out_time (new zmq::message_t (mt.str ().size ()));
    memcpy (out_time.get ()->data (), mt.str ().c_str (), mt.str ().size ());
    parts.push_back (out_time);

    std::stringstream expiry;
    expiry << m_store.get ()->get_ack_timeout ();

    boost::shared_ptr<zmq::message_t> ack_timeout (new zmq::message_t (expiry.str ().size ()));
    memcpy (ack_timeout.get ()->data (), expiry.str ().c_str (), expiry.str ().size ());
    parts.push_back (ack_timeout);

    while (true)
    {
        memcpy (&msg_size, vbuf + pos, sizeof (uint64_t));
        pos += sizeof (uint64_t);

        boost::shared_ptr<zmq::message_t> part (new zmq::message_t (msg_size));
        memcpy (part.get ()->data (), vbuf + pos, msg_size);
        parts.push_back (part);

        pos += msg_size;
        if (pos >= vsiz)
            break;
    }
    if (m_socket.get ()->send_many (parts))
        m_store->mark_in_flight (key);

    return NOP;
}

void pzq::expiry_visitor_t::run ()
{
    while (is_running ())
    {
#ifdef DEBUG
        std::cerr << "Running expiry reaper" << std::endl;
#endif
        m_time = microsecond_timestamp ();
        m_store.get ()->iterate_inflight (this);
        boost::this_thread::sleep (boost::posix_time::microseconds (m_frequency));
    }
}

const char *pzq::expiry_visitor_t::visit_full (const char *kbuf, size_t ksiz, const char *vbuf, size_t vsiz, size_t *sp)
{
    if (sizeof (uint64_t) != vsiz)
        return Visitor::NOP;

    uint64_t value;
    memcpy (&value, vbuf, sizeof (uint64_t));

	if (m_time - value > m_timeout)
	{
        m_store.get ()->message_expired ();
        return Visitor::REMOVE;
	}
    return Visitor::NOP;
}
