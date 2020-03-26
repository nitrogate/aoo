#include "aoo_sink.hpp"
#include "aoo/aoo_utils.hpp"

#include "oscpack/osc/OscOutboundPacketStream.h"
#include "oscpack/osc/OscReceivedElements.h"

#include <algorithm>

/*//////////////////// aoo_sink /////////////////////*/

aoo_sink * aoo_sink_new(int32_t id) {
    return new aoo::sink(id);
}

void aoo_sink_free(aoo_sink *sink) {
    // cast to correct type because base class
    // has no virtual destructor!
    delete static_cast<aoo::sink *>(sink);
}

int32_t aoo_sink_setup(aoo_sink *sink, int32_t samplerate,
                       int32_t blocksize, int32_t nchannels) {
    return sink->setup(samplerate, blocksize, nchannels);
}

int32_t aoo::sink::setup(int32_t samplerate,
                         int32_t blocksize, int32_t nchannels){
    if (samplerate > 0 && blocksize > 0 && nchannels > 0)
    {
        nchannels_ = nchannels;
        samplerate_ = samplerate;
        blocksize_ = blocksize;

        buffer_.resize(blocksize_ * nchannels_);

        // reset timer + time DLL filter
        timer_.setup(samplerate_, blocksize_);

        // don't need to lock
        update_sources();

        return 1;
    }
    return 0;
}

namespace aoo {

template<typename T>
T& as(void *p){
    return *reinterpret_cast<T *>(p);
}

} // aoo

#define CHECKARG(type) assert(size == sizeof(type))

int32_t aoo_sink_setoption(aoo_sink *sink, int32_t opt, void *p, int32_t size)
{
    return sink->set_option(opt, p, size);
}

int32_t aoo::sink::set_option(int32_t opt, void *ptr, int32_t size)
{
    switch (opt){
    // reset
    case aoo_opt_reset:
        update_sources();
        // reset time DLL
        timer_.reset();
        break;
    // buffer size
    case aoo_opt_buffersize:
    {
        CHECKARG(int32_t);
        auto bufsize = std::max<int32_t>(0, as<int32_t>(ptr));
        if (bufsize != buffersize_){
            buffersize_ = bufsize;
            update_sources();
        }
        break;
    }
    // ping interval
    case aoo_opt_ping_interval:
        CHECKARG(int32_t);
        ping_interval_ = std::max<int32_t>(0, as<int32_t>(ptr)) * 0.001;
        break;
    // timefilter bandwidth
    case aoo_opt_timefilter_bandwidth:
        CHECKARG(float);
        bandwidth_ = std::max<double>(0, std::min<double>(1, as<float>(ptr)));
        timer_.reset(); // will update time DLL and reset timer
        break;
    // packetsize
    case aoo_opt_packetsize:
    {
        CHECKARG(int32_t);
        const int32_t minpacketsize = 64;
        auto packetsize = as<int32_t>(ptr);
        if (packetsize < minpacketsize){
            LOG_WARNING("packet size too small! setting to " << minpacketsize);
            packetsize_ = minpacketsize;
        } else if (packetsize > AOO_MAXPACKETSIZE){
            LOG_WARNING("packet size too large! setting to " << AOO_MAXPACKETSIZE);
            packetsize_ = AOO_MAXPACKETSIZE;
        } else {
            packetsize_ = packetsize;
        }
        break;
    }
    // resend limit
    case aoo_opt_resend_limit:
        CHECKARG(int32_t);
        resend_limit_ = std::max<int32_t>(0, as<int32_t>(ptr));
        break;
    // resend interval
    case aoo_opt_resend_interval:
        CHECKARG(int32_t);
        resend_interval_ = std::max<int32_t>(0, as<int32_t>(ptr)) * 0.001;
        break;
    // resend maxnumframes
    case aoo_opt_resend_maxnumframes:
        CHECKARG(int32_t);
        resend_maxnumframes_ = std::max<int32_t>(1, as<int32_t>(ptr));
        break;
    // unknown
    default:
        LOG_WARNING("aoo_sink: unsupported option " << opt);
        return 0;
    }
    return 1;
}

int32_t aoo_sink_getoption(aoo_sink *sink, int32_t opt, void *p, int32_t size)
{
    return sink->get_option(opt, p, size);
}

int32_t aoo::sink::get_option(int32_t opt, void *ptr, int32_t size)
{
    switch (opt){
    // buffer size
    case aoo_opt_buffersize:
        CHECKARG(int32_t);
        as<int32_t>(ptr) = buffersize_;
        break;
    // ping interval
    case aoo_opt_ping_interval:
        CHECKARG(int32_t);
        as<int32_t>(ptr) = ping_interval_ * 1000;
        break;
    // timefilter bandwidth
    case aoo_opt_timefilter_bandwidth:
        CHECKARG(float);
        as<float>(ptr) = bandwidth_;
        break;
    // resend packetsize
    case aoo_opt_packetsize:
        CHECKARG(int32_t);
        as<int32_t>(ptr) = packetsize_;
        break;
    // resend limit
    case aoo_opt_resend_limit:
        CHECKARG(int32_t);
        as<int32_t>(ptr) = resend_limit_;
        break;
    // resend interval
    case aoo_opt_resend_interval:
        CHECKARG(int32_t);
        as<int32_t>(ptr) = resend_interval_ * 1000;
        break;
    // resend maxnumframes
    case aoo_opt_resend_maxnumframes:
        CHECKARG(int32_t);
        as<int32_t>(ptr) = resend_maxnumframes_;
        break;
    // unknown
    default:
        LOG_WARNING("aoo_sink: unsupported option " << opt);
        return 0;
    }
    return 1;
}

int32_t aoo_sink_setsourceoption(aoo_sink *sink, void *endpoint, int32_t id,
                              int32_t opt, void *p, int32_t size)
{
    return sink->set_sourceoption(endpoint, id, opt, p, size);
}

int32_t aoo::sink::set_sourceoption(void *endpoint, int32_t id,
                                   int32_t opt, void *ptr, int32_t size)
{
    auto src = find_source(endpoint, id);
    if (src){
        switch (opt){
        // reset
        case aoo_opt_reset:
            src->update(*this);
            break;
        // unsupported
        default:
            LOG_WARNING("aoo_sink: unsupported source option " << opt);
            return 0;
        }
        return 1;
    } else {
        return 0;
    }
}

int32_t aoo_sink_getsourceoption(aoo_sink *sink, void *endpoint, int32_t id,
                              int32_t opt, void *p, int32_t size)
{
    return sink->get_sourceoption(endpoint, id, opt, p, size);
}

int32_t aoo::sink::get_sourceoption(void *endpoint, int32_t id,
                              int32_t opt, void *p, int32_t size)
{
    auto src = find_source(endpoint, id);
    if (src){
        switch (opt){
        // format
        case aoo_opt_format:
            CHECKARG(aoo_format_storage);
            return src->get_format(as<aoo_format_storage>(p));
        // unsupported
        default:
            LOG_WARNING("aoo_sink: unsupported source option " << opt);
            return 0;
        }
        return 1;
    } else {
        return 0;
    }
}

int32_t aoo_sink_handlemessage(aoo_sink *sink, const char *data, int32_t n,
                            void *src, aoo_replyfn fn) {
    return sink->handle_message(data, n, src, fn);
}

int32_t aoo::sink::handle_message(const char *data, int32_t n, void *endpoint, aoo_replyfn fn){
    osc::ReceivedPacket packet(data, n);
    osc::ReceivedMessage msg(packet);

    if (samplerate_ == 0){
        return 0; // not setup yet
    }

    int32_t sinkid = 0;
    auto onset = aoo_parsepattern(data, n, &sinkid);
    if (!onset){
        LOG_WARNING("not an AoO message!");
        return 0;
    }
    if (sinkid != id_ && sinkid != AOO_ID_WILDCARD){
        LOG_WARNING("wrong sink ID!");
        return 0;
    }

    if (!strcmp(msg.AddressPattern() + onset, AOO_FORMAT)){
        if (msg.ArgumentCount() == AOO_FORMAT_NARGS){
            auto it = msg.ArgumentsBegin();
            try {
                int32_t id = (it++)->AsInt32();
                int32_t salt = (it++)->AsInt32();
                // get format from arguments
                aoo_format f;
                f.nchannels = (it++)->AsInt32();
                f.samplerate = (it++)->AsInt32();
                f.blocksize = (it++)->AsInt32();
                f.codec = (it++)->AsString();
                const void *blobdata;
                osc::osc_bundle_element_size_t blobsize;
                (it++)->AsBlob(blobdata, blobsize);

                return handle_format_message(endpoint, fn, id, salt, f,
                                          (const char *)blobdata, blobsize);
            } catch (const osc::Exception& e){
                LOG_ERROR(e.what());
            }
        } else {
            LOG_ERROR("wrong number of arguments for /format message");
        }
    } else if (!strcmp(msg.AddressPattern() + onset, AOO_DATA)){
        if (msg.ArgumentCount() == AOO_DATA_NARGS){
            auto it = msg.ArgumentsBegin();
            try {
                // get header from arguments
                auto id = (it++)->AsInt32();
                auto salt = (it++)->AsInt32();
                aoo::data_packet d;
                d.sequence = (it++)->AsInt32();
                d.samplerate = (it++)->AsDouble();
                d.channel = (it++)->AsInt32();
                d.totalsize = (it++)->AsInt32();
                d.nframes = (it++)->AsInt32();
                d.framenum = (it++)->AsInt32();
                const void *blobdata;
                osc::osc_bundle_element_size_t blobsize;
                (it++)->AsBlob(blobdata, blobsize);
                d.data = (const char *)blobdata;
                d.size = blobsize;

                return handle_data_message(endpoint, fn, id, salt, d);
            } catch (const osc::Exception& e){
                LOG_ERROR(e.what());
            }
        } else {
            LOG_ERROR("wrong number of arguments for /data message");
        }
    } else {
        LOG_WARNING("unknown message '" << (msg.AddressPattern() + onset) << "'");
    }
    return 0;
}

#if AOO_DEBUG_RESAMPLING
thread_local int32_t debug_counter = 0;
#endif

int32_t aoo_sink_process(aoo_sink *sink, aoo_sample **data,
                         int32_t nsamples, uint64_t t) {
    return sink->process(data, nsamples, t);
}

#define AOO_MAXNUMEVENTS 256

int32_t aoo::sink::process(aoo_sample **data, int32_t nsamples, uint64_t t){
    std::fill(buffer_.begin(), buffer_.end(), 0);

    bool didsomething = false;

    // update time DLL filter
    double error;
    auto state = timer_.update(t, error);
    if (state == timer::state::reset){
        LOG_VERBOSE("setup time DLL filter for sink");
        dll_.setup(samplerate_, blocksize_, bandwidth_, 0);
    } else if (state == timer::state::error){
        // recover sources
        for (auto& s : sources_){
            s.recover();
        }
        timer_.reset();
    } else {
        auto elapsed = timer_.get_elapsed();
        dll_.update(elapsed);
    #if AOO_DEBUG_DLL
        DO_LOG("time elapsed: " << elapsed << ", period: " << dll_.period()
               << ", samplerate: " << dll_.samplerate());
    #endif
    }

    // the mutex is uncontended most of the time, but LATER we might replace
    // this with a lockless and/or waitfree solution
    for (auto& src : sources_){
        if (src.process(*this, buffer_.data(), buffer_.size())){
            didsomething = true;
        }
    }

    if (didsomething){
    #if AOO_CLIP_OUTPUT
        for (auto it = buffer_.begin(); it != buffer_.end(); ++it){
            if (*it > 1.0){
                *it = 1.0;
            } else if (*it < -1.0){
                *it = -1.0;
            }
        }
    #endif
        // copy buffers
        for (int i = 0; i < nchannels_; ++i){
            auto buf = &buffer_[i * blocksize_];
            std::copy(buf, buf + blocksize_, data[i]);
        }
        return 1;
    } else {
        return 0;
    }
}
int32_t aoo_sink_eventsavailable(aoo_sink *sink){
    return sink->events_available();
}

int32_t aoo::sink::events_available(){
    for (auto& src : sources_){
        if (src.has_events()){
            return true;
        }
    }
    return false;
}

int32_t aoo_sink_handleevents(aoo_sink *sink,
                              aoo_eventhandler fn, void *user){
    return sink->handle_events(fn, user);
}

#define EVENT_THROTTLE 1000

int32_t aoo::sink::handle_events(aoo_eventhandler fn, void *user){
    if (!fn){
        return 0;
    }
    int total = 0;
    // handle_events() and the source list itself are both lock-free!
    for (auto& src : sources_){
        total += src.handle_events(fn, user);
        if (total > EVENT_THROTTLE){
            break;
        }
    }
    return total;
}

namespace aoo {

aoo::source_desc * sink::find_source(void *endpoint, int32_t id){
    for (auto& src : sources_){
        if ((src.endpoint() == endpoint) && (src.id() == id)){
            return &src;
        }
    }
    return nullptr;
}

void sink::update_sources(){
    for (auto& src : sources_){
        src.update(*this);
    }
}

int32_t sink::handle_format_message(void *endpoint, aoo_replyfn fn, int32_t id,
                           int32_t salt, const aoo_format& format,
                           const char *settings, int32_t size)
{
    if (id < 0){
        LOG_WARNING("bad ID for " << AOO_FORMAT << " message");
        return 0;
    }
    // try to find existing source
    auto src = find_source(endpoint, id);

    if (!src){
        // not found - add new source
        sources_.emplace_front(endpoint, fn, id, salt);
        src = &sources_.front();
    }

    return src->handle_format(*this, salt, format, settings, size);
}

int32_t sink::handle_data_message(void *endpoint, aoo_replyfn fn, int32_t id,
                           int32_t salt, const data_packet& data)
{
    if (id < 0){
        LOG_WARNING("bad ID for " << AOO_DATA << " message");
        return 0;
    }
    // try to find existing source
    auto src = find_source(endpoint, id);
    if (src){
        return src->handle_data(*this, salt, data);
    } else {
        // discard data message and request format!
        request_format(endpoint, fn, id);
        return 0;
    }
}

// /AoO/<src>/request <sink>

void sink::request_format(void *endpoint, aoo_replyfn fn, int32_t id) const {
    LOG_VERBOSE("request format for source " << id);
    char buf[AOO_MAXPACKETSIZE];
    osc::OutboundPacketStream msg(buf, sizeof(buf));

    // make OSC address pattern
    const int32_t max_addr_size = sizeof(AOO_DOMAIN) + 16 + sizeof(AOO_REQUEST);
    char address[max_addr_size];
    snprintf(address, sizeof(address), "%s/%d%s", AOO_DOMAIN, id, AOO_REQUEST);

    msg << osc::BeginMessage(address) << id_ << osc::EndMessage;

    fn(endpoint, msg.Data(), msg.Size());
}


/*////////////////////////// source_desc /////////////////////////////*/

source_desc::source_desc(void *endpoint, aoo_replyfn fn, int32_t id, int32_t salt)
    : endpoint_(endpoint), fn_(fn), id_(id), salt_(salt)
{
    eventqueue_.resize(AOO_EVENTQUEUESIZE, 1);
    // push "add" event
    aoo_event event;
    event.type = AOO_SOURCE_ADD_EVENT;
    event.source.endpoint = endpoint;
    event.source.id = id;
    eventqueue_.write(event);
    LOG_DEBUG("add new source with id " << id);
}

int32_t source_desc::get_format(aoo_format_storage &format){
    // synchronize with handle_format() and update()!
    shared_lock lock(mutex_);
    if (decoder_){
        return decoder_->get_format(format);
    } else {
        return 0;
    }
}

void source_desc::update(const sink &s){
    // take writer lock!
    unique_lock lock(mutex_);
    do_update(s);
}

void source_desc::do_update(const sink &s){
    // resize audio ring buffer
    if (decoder_ && decoder_->blocksize() > 0 && decoder_->samplerate() > 0){
        // recalculate buffersize from ms to samples
        double bufsize = (double)s.buffersize() * decoder_->samplerate() * 0.001;
        auto d = div(bufsize, decoder_->blocksize());
        int32_t nbuffers = d.quot + (d.rem != 0); // round up
        nbuffers = std::max<int32_t>(1, nbuffers); // e.g. if buffersize_ is 0
        // resize audio buffer and initially fill with zeros.
        auto nsamples = decoder_->nchannels() * decoder_->blocksize();
        audioqueue_.resize(nbuffers * nsamples, nsamples);
        infoqueue_.resize(nbuffers, 1);
        int count = 0;
        while (audioqueue_.write_available() && infoqueue_.write_available()){
            audioqueue_.write_commit();
            // push nominal samplerate + default channel (0)
            block_info i;
            i.sr = decoder_->samplerate();
            i.channel = 0;
            infoqueue_.write(i);
            count++;
        };
        LOG_DEBUG("write " << count << " silent blocks");
    #if 0
        // don't touch the event queue once constructed
        eventqueue_.reset();
    #endif
        // setup resampler
        resampler_.setup(decoder_->blocksize(), s.blocksize(),
                            decoder_->samplerate(), s.samplerate(), decoder_->nchannels());
        // resize block queue
        blockqueue_.resize(nbuffers);
        newest_ = 0;
        next_ = -1;
        channel_ = 0;
        samplerate_ = decoder_->samplerate();
        lastpingtime_ = 0;
        laststate_ = AOO_SOURCE_STATE_STOP;
        streamstate_.reset();
        ack_list_.set_limit(s.resend_limit());
        ack_list_.clear();
        LOG_DEBUG("update source " << id_ << ": sr = " << decoder_->samplerate()
                    << ", blocksize = " << decoder_->blocksize() << ", nchannels = "
                    << decoder_->nchannels() << ", bufsize = " << nbuffers * nsamples);
    }
}

// /AoO/<sink>/format <src> <salt> <numchannels> <samplerate> <blocksize> <codec> <settings...>

int32_t source_desc::handle_format(const sink& s, int32_t salt, const aoo_format& f,
                                   const char *settings, int32_t size){
    // take writer lock!
    unique_lock lock(mutex_);

    salt_ = salt;

    // create/change decoder if needed
    if (!decoder_ || strcmp(decoder_->name(), f.codec)){
        auto c = aoo::find_codec(f.codec);
        if (c){
            decoder_ = c->create_decoder();
        } else {
            LOG_ERROR("codec '" << f.codec << "' not supported!");
            return 0;
        }
        if (!decoder_){
            LOG_ERROR("couldn't create decoder!");
            return 0;
        }
    }

    // read format
    decoder_->read_format(f.nchannels, f.samplerate, f.blocksize, settings, size);

    do_update(s);

    // push event
    aoo_event event;
    event.type = AOO_SOURCE_FORMAT_EVENT;
    event.source.endpoint = endpoint_;
    event.source.id = id_;
    eventqueue_.write(event);

    return 1;
}

// /AoO/<sink>/data <src> <salt> <seq> <sr> <channel_onset> <totalsize> <numpackets> <packetnum> <data>

int32_t source_desc::handle_data(const sink& s, int32_t salt, const aoo::data_packet& d){
    // synchronize with update()!
    shared_lock lock(mutex_);

    // the source format might have changed and we haven't noticed,
    // e.g. because of dropped UDP packets.
    if (salt != salt_){
        s.request_format(endpoint_, fn_, id_);
        return 0;
    }

#if 1
    if (!decoder_){
        LOG_DEBUG("ignore data message");
        return 0;
    }
#else
    assert(decoder_ != nullptr);
#endif
    LOG_DEBUG("got block: seq = " << d.sequence << ", sr = " << d.samplerate
              << ", chn = " << d.channel << ", totalsize = " << d.totalsize
              << ", nframes = " << d.nframes << ", frame = " << d.framenum << ", size " << d.size);

    if (next_ < 0){
        next_ = d.sequence;
    }

    // check data packet
    if (!check_packet(d)){
        return 0;
    }

    // add data packet
    if (!add_packet(d)){
        return 0;
    }

    // try to send blocks
    send_data();

#if 1
    pop_outdated_blocks();
#endif

    // check and resend missing blocks
    check_missing_blocks(s);

    // we can release the lock now!
    // this makes sure that the reply function in send()
    // is called without any lock to avoid deadlocks.
    lock.unlock();

    // send request messages
    request_data(s, salt);

    // ping source
    ping(s);

    return 1;
}

bool source_desc::process(const sink& s, aoo_sample *buffer, int32_t size){
    // synchronize with handle_format() and update()!
    // the mutex should be uncontended most of the time.
    // NOTE: We could use try_lock() and skip the block if we couldn't aquire the lock.
    shared_lock lock(mutex_);

    if (!decoder_){
        return false;
    }

    int32_t nsamples = audioqueue_.blocksize();

    while (audioqueue_.read_available() && infoqueue_.read_available()
           && resampler_.write_available() >= nsamples){
        // get block info and set current channel + samplerate
        block_info info;
        infoqueue_.read(info);
        channel_ = info.channel;
        samplerate_ = info.sr;

        // write audio into resampler
        resampler_.write(audioqueue_.read_data(), nsamples);
        audioqueue_.read_commit();

        // record stream state
        int32_t lost, reordered, resent, gap;
        streamstate_.get(lost, reordered, resent, gap);

        aoo_event event;
        event.source.endpoint = endpoint_;
        event.source.id = id_;
        if (lost > 0){
            // push packet loss event
            event.type = AOO_BLOCK_LOSS_EVENT;
            event.block_loss.count = lost;
            eventqueue_.write(event);
        }
        if (reordered > 0){
            // push packet reorder event
            event.type = AOO_BLOCK_REORDER_EVENT;
            event.block_reorder.count = reordered;
            eventqueue_.write(event);
        }
        if (resent > 0){
            // push packet resend event
            event.type = AOO_BLOCK_RESEND_EVENT;
            event.block_resend.count = resent;
            eventqueue_.write(event);
        }
        if (gap > 0){
            // push packet gap event
            event.type = AOO_BLOCK_GAP_EVENT;
            event.block_gap.count = gap;
            eventqueue_.write(event);
        }
    }
    // update resampler
    resampler_.update(samplerate_, s.real_samplerate());
    // read samples from resampler
    auto nchannels = decoder_->nchannels();
    auto readsamples = s.blocksize() * nchannels;
    if (resampler_.read_available() >= readsamples){
        auto buf = (aoo_sample *)alloca(readsamples * sizeof(aoo_sample));
        resampler_.read(buf, readsamples);

        // sum source into sink (interleaved -> non-interleaved),
        // starting at the desired sink channel offset.
        // out of bound source channels are silently ignored.
        for (int i = 0; i < nchannels; ++i){
            auto chn = i + channel_;
            // ignore out-of-bound source channels!
            if (chn < s.nchannels()){
                auto n = s.blocksize();
                auto out = buffer + n * chn;
                for (int j = 0; j < n; ++j){
                    out[j] += buf[j * nchannels + i];
                }
            }
        }

        LOG_DEBUG("read samples from source " << id_);

        if (laststate_ != AOO_SOURCE_STATE_START){
            // push "start" event
            aoo_event event;
            event.type = AOO_SOURCE_STATE_EVENT;
            event.source.endpoint = endpoint_;
            event.source.id = id_;
            event.source_state.state = AOO_SOURCE_STATE_START;
            eventqueue_.write(event);
            laststate_ = AOO_SOURCE_STATE_START;
        }

        return true;
    } else {
        // buffer ran out -> push "stop" event
        if (laststate_ != AOO_SOURCE_STATE_STOP){
            aoo_event event;
            event.type = AOO_SOURCE_STATE_EVENT;
            event.source.endpoint = endpoint_;
            event.source.id = id_;
            event.source_state.state = AOO_SOURCE_STATE_STOP;
            eventqueue_.write(event);
            laststate_ = AOO_SOURCE_STATE_STOP;
        }

        return false;
    }
}

int32_t source_desc::handle_events(aoo_eventhandler handler, void *user){
    // copy events - always lockfree! (the eventqueue is never resized)
    auto n = eventqueue_.read_available();
    auto events = (aoo_event *)alloca(sizeof(aoo_event) * n);
    for (int i = 0; i < n; ++i){
        eventqueue_.read(events[i]);
    }
    handler(user, events, n);
    return n;
}

bool source_desc::check_packet(const data_packet &d){
    if (d.sequence < next_){
        // block too old, discard!
        LOG_VERBOSE("discarded old block " << d.sequence);
        return false;
    }
    auto diff = d.sequence - newest_;

    // check for large gap between incoming block and most recent block
    // (either network problem or stream has temporarily stopped.)
    bool large_gap = newest_ > 0 && diff > blockqueue_.capacity();

    // check if we need to recover
    bool recover = streamstate_.get_recover();

    // check for empty block (= skipped)
    bool dropped = d.totalsize == 0;

    // check and update newest sequence number
    if (diff < 0){
        // TODO the following distinction doesn't seem to work reliably.
        if (ack_list_.find(d.sequence)){
            LOG_DEBUG("resent block " << d.sequence);
            streamstate_.add_resent(1);
        } else {
            LOG_VERBOSE("block " << d.sequence << " out of order!");
            streamstate_.add_reordered(1);
        }
    } else {
        if (newest_ > 0 && diff > 1){
            LOG_VERBOSE("skipped " << (diff - 1) << " blocks");
        }
        // update newest sequence number
        newest_ = d.sequence;
    }

    if (large_gap || recover || dropped){
        // record dropped blocks
        streamstate_.add_lost(blockqueue_.size());
        if (diff > 1){
            streamstate_.add_gap(diff - 1);
        }
        // clear the block queue and fill audio buffer with zeros.
        blockqueue_.clear();
        ack_list_.clear();
        next_ = d.sequence;
        // push silent blocks to keep the buffer full, but leave room for one block!
        int count = 0;
        auto nsamples = audioqueue_.blocksize();
        while (audioqueue_.write_available() > 1 && infoqueue_.write_available() > 1){
            auto ptr = audioqueue_.write_data();
            for (int i = 0; i < nsamples; ++i){
                ptr[i] = 0;
            }
            audioqueue_.write_commit();
            // push nominal samplerate + default channel (0)
            block_info i;
            i.sr = decoder_->samplerate();
            i.channel = 0;
            infoqueue_.write(i);

            count++;
        }
        LOG_VERBOSE("wrote " << count << " silent blocks for "
                    << (large_gap ? "transmission gap" :
                        recover ? "recovery" : "host timing gap"));
        if (dropped){
            next_++;
            return false;
        }
    }
    return true;
}

bool source_desc::add_packet(const data_packet& d){
    auto block = blockqueue_.find(d.sequence);
    if (!block){
        if (blockqueue_.full()){
            // if the queue is full, we have to drop a block;
            // in this case we send a block of zeros to the audio buffer
            auto old = blockqueue_.front().sequence;
            auto nsamples = audioqueue_.blocksize();
            if (audioqueue_.write_available() && infoqueue_.write_available()){
                auto ptr = audioqueue_.write_data();
                for (int i = 0; i < nsamples; ++i){
                    ptr[i] = 0;
                }
                audioqueue_.write_commit();
                // push nominal samplerate + default channel (0)
                block_info i;
                i.sr = decoder_->samplerate();
                i.channel = 0;
                infoqueue_.write(i);
                // update 'next'
                if (next_ <= old){
                    next_ = old + 1;
                }
            }
            LOG_VERBOSE("dropped block " << old);
            // remove block from acklist
            ack_list_.remove(old);
            // record dropped block
            streamstate_.add_lost(1);
        }
        // add new block
        block = blockqueue_.insert(d.sequence, d.samplerate,
                                   d.channel, d.totalsize, d.nframes);
    } else if (block->has_frame(d.framenum)){
        LOG_VERBOSE("frame " << d.framenum << " of block " << d.sequence << " already received!");
        return false;
    }

    // add frame to block
    block->add_frame(d.framenum, (const char *)d.data, d.size);

#if 1
    if (block->complete()){
        // remove block from acklist as early as possible
        ack_list_.remove(block->sequence);
    }
#endif
    return true;
}

void source_desc::send_data(){
    // Transfer all consecutive complete blocks as long as
    // no previous (expected) blocks are missing.
    if (blockqueue_.empty()){
        return;
    }

    auto b = blockqueue_.begin();
    int32_t count = 0;
    int32_t next = next_;
    while ((b != blockqueue_.end()) && b->complete()
           && (b->sequence == next)
           && audioqueue_.write_available() && infoqueue_.write_available())
    {
        LOG_DEBUG("write samples (" << b->sequence << ")");

        auto ptr = audioqueue_.write_data();
        auto nsamples = audioqueue_.blocksize();
        assert(b->data() != nullptr && b->size() > 0 && ptr != nullptr && nsamples > 0);
        // decode audio data
        if (decoder_->decode(b->data(), b->size(), ptr, nsamples) <= 0){
            LOG_VERBOSE("bad block: size = " << b->size() << ", nsamples = " << nsamples);
            // decoder failed - fill with zeros
            std::fill(ptr, ptr + nsamples, 0);
        }

        audioqueue_.write_commit();

        // push info
        block_info i;
        i.sr = b->samplerate;
        i.channel = b->channel;
        infoqueue_.write(i);

        count++;
        b++;
        next++;
    }
    next_ = next;
    // pop blocks
    while (count--){
    #if 0
        // remove block from acklist
        ack_list_.remove(blockqueue_.front().sequence);
    #endif
        // pop block
        LOG_DEBUG("pop block " << blockqueue_.front().sequence);
        blockqueue_.pop_front();
    }
    LOG_DEBUG("next: " << next_);
}

void source_desc::pop_outdated_blocks(){
    // pop outdated blocks (shouldn't really happen...)
    while (!blockqueue_.empty() &&
           (newest_ - blockqueue_.front().sequence) >= blockqueue_.capacity())
    {
        auto old = blockqueue_.front().sequence;
        LOG_VERBOSE("pop outdated block " << old);
        // remove block from acklist
        ack_list_.remove(old);
        // pop block
        blockqueue_.pop_front();
        // update 'next'
        if (next_ <= old){
            next_ = old + 1;
        }
        // record dropped block
        streamstate_.add_lost(1);
    }
}

#define AOO_BLOCKQUEUE_CHECK_THRESHOLD 3

// deal with "holes" in block queue
void source_desc::check_missing_blocks(const sink& s){
    if (blockqueue_.empty()){
        if (!ack_list_.empty()){
            LOG_WARNING("bug: acklist not empty");
            ack_list_.clear();
        }
        return;
    }
    // don't check below a certain threshold,
    // because we might just experience packet reordering.
    if (blockqueue_.size() < AOO_BLOCKQUEUE_CHECK_THRESHOLD){
        return;
    }
#if LOGLEVEL >= 4
    std::cerr << queue << std::endl;
#endif
    int32_t numframes = 0;

    // resend incomplete blocks except for the last block
    LOG_DEBUG("resend incomplete blocks");
    for (auto it = blockqueue_.begin(); it != (blockqueue_.end() - 1); ++it){
        if (!it->complete()){
            // insert ack (if needed)
            auto& ack = ack_list_.get(it->sequence);
            if (ack.check(s.elapsed_time(), s.resend_interval())){
                for (int i = 0; i < it->num_frames(); ++i){
                    if (!it->has_frame(i)){
                        if (numframes < s.resend_maxnumframes()){
                            retransmit_list_.push_back(data_request { it->sequence, i });
                            numframes++;
                        } else {
                            goto resend_incomplete_done;
                        }
                    }
                }
            }
        }
    }
resend_incomplete_done:

    // resend missing blocks before any (half)completed blocks
    LOG_DEBUG("resend missing blocks");
    int32_t next = next_;
    for (auto it = blockqueue_.begin(); it != blockqueue_.end(); ++it){
        auto missing = it->sequence - next;
        if (missing > 0){
            for (int i = 0; i < missing; ++i){
                // insert ack (if necessary)
                auto& ack = ack_list_.get(next + i);
                if (ack.check(s.elapsed_time(), s.resend_interval())){
                    if (numframes + it->num_frames() <= s.resend_maxnumframes()){
                        retransmit_list_.push_back(data_request { next + i, -1 }); // whole block
                        numframes += it->num_frames();
                    } else {
                        goto resend_missing_done;
                    }
                }
            }
        } else if (missing < 0){
            LOG_VERBOSE("bug: sequence = " << it->sequence << ", next = " << next);
            assert(false);
        }
        next = it->sequence + 1;
    }
resend_missing_done:

    assert(numframes <= s.resend_maxnumframes());
    if (numframes > 0){
        LOG_DEBUG("requested " << numframes << " frames");
    }

#if 1
    // clean ack list
    auto removed = ack_list_.remove_before(next_);
    if (removed > 0){
        LOG_DEBUG("block_ack_list: removed " << removed << " outdated items");
    }
#endif

#if LOGLEVEL >= 4
    std::cerr << acklist << std::endl;
#endif
}

// /AoO/<src>/resend <sink> <salt> <seq0> <frame0> <seq1> <frame1> ...

void source_desc::request_data(const sink &s, int32_t salt){
    // called without lock!
    if (retransmit_list_.empty()){
        return;
    }

    // send request messages
    char buf[AOO_MAXPACKETSIZE];
    osc::OutboundPacketStream msg(buf, sizeof(buf));

    // make OSC address pattern
    const int32_t maxaddrsize = sizeof(AOO_DOMAIN) + 16 + sizeof(AOO_RESEND);
    char address[maxaddrsize];
    snprintf(address, sizeof(address), "%s/%d%s", AOO_DOMAIN, id_, AOO_RESEND);

    const int32_t maxdatasize = s.packetsize() - maxaddrsize - 16; // id + salt + padding
    const int32_t maxrequests = maxdatasize / 10; // 2 * (int32_t + typetag)
    auto d = div(retransmit_list_.size(), maxrequests);

    auto dorequest = [&](const data_request* data, int32_t n){
        msg << osc::BeginMessage(address) << s.id() << salt;
        for (int i = 0; i < n; ++i){
            msg << data[i].sequence << data[i].frame;
        }
        msg << osc::EndMessage;

        send(msg.Data(), msg.Size());
    };

    for (int i = 0; i < d.quot; ++i){
        dorequest(retransmit_list_.data() + i * maxrequests, maxrequests);
    }
    if (d.rem > 0){
        dorequest(retransmit_list_.data() + retransmit_list_.size() - d.rem, d.rem);
    }

    retransmit_list_.clear();
}

// AoO/<id>/ping <sink>

void source_desc::ping(const sink& s){
    // called without lock!

    if (s.ping_interval() == 0){
        return;
    }
    auto now = s.elapsed_time();
    if ((now - lastpingtime_) > s.ping_interval()){
        char buffer[AOO_MAXPACKETSIZE];
        osc::OutboundPacketStream msg(buffer, sizeof(buffer));

        // make OSC address pattern
        const int32_t max_addr_size = sizeof(AOO_DOMAIN) + 16 + sizeof(AOO_PING);
        char address[max_addr_size];
        snprintf(address, sizeof(address), "%s/%d%s", AOO_DOMAIN, id_, AOO_PING);

        msg << osc::BeginMessage(address) << s.id() << osc::EndMessage;

        send(msg.Data(), msg.Size());

        lastpingtime_ = now;

        LOG_DEBUG("send ping to source " << id_);
    }
}

} // aoo
