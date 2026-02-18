#include "../Utils/log_shm.hpp"
#include <iostream>
#include <signal.h>
#include <chrono>
#include <thread>

volatile sig_atomic_t running = 1;

void signal_handler(int)
{
        running = 0;
}

// Binary file logger - fastest, no parsing overhead
template <typename LogT, size_t QueueSize = 65536>
class BinaryFileLogger
{
        ShmQueue<LogT, QueueSize> queue;
        int fd;
        size_t total_written;

public:
        BinaryFileLogger() : fd(-1), total_written(0) {}

        bool init(const char *shm_name, const char *log_file)
        {
                if (!queue.attach(shm_name))
                {
                        std::cerr << "Failed to attach to SHM: " << shm_name << std::endl;
                        return false;
                }

                fd = open(log_file, O_WRONLY | O_CREAT | O_APPEND, 0644);
                if (fd == -1)
                {
                        std::cerr << "Failed to open log file: " << log_file << std::endl;
                        return false;
                }

                std::cout << "Logger initialized. SHM: " << shm_name
                          << ", File: " << log_file << std::endl;
                return true;
        }

        void run(size_t batch_size = 1024)
        {
                LogT *buffer = new LogT[batch_size];
                size_t idle_count = 0;

                std::cout << "Logger running (batch_size=" << batch_size << ")..." << std::endl;

                while (running)
                {
                        size_t count = 0;

                        // Batch dequeue
                        while (count < batch_size && queue.pop(buffer[count]))
                        {
                                ++count;
                        }
                        //
                        if (count > 0)
                        {
                                // Write batch to file
                                ssize_t written = write(fd, buffer, count * sizeof(LogT));
                                if (written > 0)
                                {
                                        total_written += written;
                                }
                                idle_count = 0;
                        }
                        else
                        {
                                // Queue empty - back off
                                idle_count++;
                                if (idle_count < 1000)
                                {
                                        // Spin briefly
                                        std::this_thread::yield();
                                }
                                else if (idle_count < 10000)
                                {
                                        // Short sleep
                                        usleep(1);
                                }
                                else
                                {
                                        // Longer sleep
                                        usleep(100);
                                }
                        }
                }

                // Flush remaining
                flush_remaining(buffer, batch_size);

                delete[] buffer;
                std::cout << "Logger stopped. Total written: " << total_written << " bytes" << std::endl;
        }

        void flush_remaining(LogT *buffer, size_t batch_size)
        {
                size_t count = 0;
                while (queue.pop(buffer[count]) && count < batch_size)
                {
                        ++count;
                }
                if (count > 0)
                {
                        write(fd, buffer, count * sizeof(LogT));
                }
                fsync(fd);
        }

        ~BinaryFileLogger()
        {
                if (fd != -1)
                {
                        fsync(fd);
                        close(fd);
                }
        }
};

// Text file logger - human readable, slower
template <typename LogT, size_t QueueSize = 65536>
class TextFileLogger
{
        ShmQueue<LogT, QueueSize> queue;
        FILE *fp;
        size_t total_written;

public:
        TextFileLogger() : fp(nullptr), total_written(0) {}

        bool init(const char *shm_name, const char *log_file)
        {
                if (!queue.attach(shm_name))
                {
                        std::cerr << "Failed to attach to SHM: " << shm_name << std::endl;
                        return false;
                }

                fp = fopen(log_file, "a");
                if (!fp)
                {
                        std::cerr << "Failed to open log file: " << log_file << std::endl;
                        return false;
                }

                // Large buffer for better performance
                setvbuf(fp, nullptr, _IOFBF, 256 * 1024);

                std::cout << "Text logger initialized. SHM: " << shm_name
                          << ", File: " << log_file << std::endl;
                return true;
        }

        // Override this for custom formatting
        virtual void format_log(const LogT &entry, FILE *out) = 0;

        void run(size_t batch_size = 1024)
        {
                LogT *buffer = new LogT[batch_size];
                size_t idle_count = 0;

                std::cout << "Text logger running..." << std::endl;

                while (running)
                {
                        size_t count = 0;

                        while (count < batch_size && queue.pop(buffer[count]))
                        {
                                ++count;
                        }

                        if (count > 0)
                        {
                                for (size_t i = 0; i < count; ++i)
                                {
                                        format_log(buffer[i], fp);
                                }
                                fflush(fp);
                                total_written += count;
                                idle_count = 0;
                        }
                        else
                        {
                                idle_count++;
                                if (idle_count < 1000)
                                {
                                        std::this_thread::yield();
                                }
                                else
                                {
                                        usleep(100);
                                }
                        }
                }

                // Flush remaining
                LogT item;
                while (queue.pop(item))
                {
                        format_log(item, fp);
                }
                fflush(fp);

                delete[] buffer;
                std::cout << "Text logger stopped. Total entries: " << total_written << std::endl;
        }

        ~TextFileLogger()
        {
                if (fp)
                {
                        fflush(fp);
                        fclose(fp);
                }
        }
};

// Example: TradeLog text formatter
class TradeLogTextLogger : public TextFileLogger<TradeLog>
{
public:
        void format_log(const TradeLog &entry, FILE *out) override
        {
                fprintf(out, "%lu,%lu,%.2f,%u,%s,%s\n",
                        entry.timestamp_ns,
                        entry.order_id,
                        entry.price,
                        entry.quantity,
                        entry.symbol,
                        entry.side == 0 ? "BUY" : "SELL");
        }
};

// Example: MarketData text formatter
class StrategyDataLog_TextLogger : public TextFileLogger<StrategyDataLog>
{
public:
        void format_log(const StrategyDataLog &entry, FILE *out) override
        {
//                 enum class StrategyState : uint8_t
// {
//     NewOrder,
//     CancelOrder,
//     ModifyOrder

// };
                char msg_char = '?';

                switch (entry.msg_type)
                {
                case StrategyState::NewOrder:    msg_char = 'N'; break;
                case StrategyState::CancelOrder: msg_char = 'X'; break;
                case StrategyState::ModifyOrder: msg_char = 'M'; break;
                }

                // Basic strategy data
                fprintf(out, "msg_type=%c,pf_id=%u,oms_order_id=%u,token=%u,price=%.2f,qty=%u,side=%d,current_spread=%d,given_spread=%d,diff=%d",
                        msg_char,
                        entry.pf_id,
                        entry.oms_order_id,
                        entry.token,
                        entry.price / 100.0,
                        entry.qty,
                        static_cast<int>(entry.side),
                        entry.current_spread,
                        entry.given_spread,
                        entry.diff);

                // Market snapshot metadata
                const auto &snapshot = entry.market_snapshot;
                fprintf(out, ",snapshot_kind=%d,timestamp=%lu,is_valid=%d",
                        static_cast<int>(snapshot.kind),
                        snapshot.timestamp,
                        snapshot.is_valid ? 1 : 0);

                switch (snapshot.kind)
                {
                case StrategyKind::CONREV_IOC:
                {

                        const StoredMarketDataLatency *fut = nullptr;
                        const StoredMarketDataLatency *call = nullptr;
                        const StoredMarketDataLatency *put = nullptr;
                        fut = &snapshot.data.conrev.fut;
                        call = &snapshot.data.conrev.call;
                        put = &snapshot.data.conrev.put;
                        if (fut && call && put)
                        {
                                // Future data
                                fprintf(out, ",fut_start_time=%llu,fut_seqno=%u,fut_internal_seqno=%u,fut_stream_id=%u,fut_msg_type=%c,fut_ltp=%.2f",
                                        fut->start_time, fut->seqno, fut->internal_seqno, fut->stream_id, fut->msg_type, fut->last_traded_price / 100.0);
                                fprintf(out, ",fut_bids=[%.2f;%.2f;%.2f;%.2f;%.2f]",
                                        fut->bids[0] / 100.0, fut->bids[1] / 100.0, fut->bids[2] / 100.0, fut->bids[3] / 100.0, fut->bids[4] / 100.0);
                                fprintf(out, ",fut_bids_qty=[%u;%u;%u;%u;%u]",
                                        fut->bids_qty[0], fut->bids_qty[1], fut->bids_qty[2], fut->bids_qty[3], fut->bids_qty[4]);
                                fprintf(out, ",fut_asks=[%.2f;%.2f;%.2f;%.2f;%.2f]",
                                        fut->asks[0] / 100.0, fut->asks[1] / 100.0, fut->asks[2] / 100.0, fut->asks[3] / 100.0, fut->asks[4] / 100.0);
                                fprintf(out, ",fut_asks_qty=[%u;%u;%u;%u;%u]",
                                        fut->asks_qty[0], fut->asks_qty[1], fut->asks_qty[2], fut->asks_qty[3], fut->asks_qty[4]);

                                // Call data
                                fprintf(out, ",call_start_time=%llu,call_seqno=%u,call_internal_seqno=%u,call_stream_id=%u,call_msg_type=%c,call_ltp=%.2f",
                                        call->start_time, call->seqno, call->internal_seqno, call->stream_id, call->msg_type, call->last_traded_price / 100.0);
                                fprintf(out, ",call_bids=[%.2f;%.2f;%.2f;%.2f;%.2f]",
                                        call->bids[0] / 100.0, call->bids[1] / 100.0, call->bids[2] / 100.0, call->bids[3] / 100.0, call->bids[4] / 100.0);
                                fprintf(out, ",call_bids_qty=[%u;%u;%u;%u;%u]",
                                        call->bids_qty[0], call->bids_qty[1], call->bids_qty[2], call->bids_qty[3], call->bids_qty[4]);
                                fprintf(out, ",call_asks=[%.2f;%.2f;%.2f;%.2f;%.2f]",
                                        call->asks[0] / 100.0, call->asks[1] / 100.0, call->asks[2] / 100.0, call->asks[3] / 100.0, call->asks[4] / 100.0);
                                fprintf(out, ",call_asks_qty=[%u;%u;%u;%u;%u]",
                                        call->asks_qty[0], call->asks_qty[1], call->asks_qty[2], call->asks_qty[3], call->asks_qty[4]);

                                // Put data
                                fprintf(out, ",put_start_time=%llu,put_seqno=%u,put_internal_seqno=%u,put_stream_id=%u,put_msg_type=%c,put_ltp=%.2f",
                                        put->start_time, put->seqno, put->internal_seqno, put->stream_id, put->msg_type, put->last_traded_price / 100.0);
                                fprintf(out, ",put_bids=[%.2f;%.2f;%.2f;%.2f;%.2f]",
                                        put->bids[0] / 100.0, put->bids[1] / 100.0, put->bids[2] / 100.0, put->bids[3] / 100.0, put->bids[4] / 100.0);
                                fprintf(out, ",put_bids_qty=[%u;%u;%u;%u;%u]",
                                        put->bids_qty[0], put->bids_qty[1], put->bids_qty[2], put->bids_qty[3], put->bids_qty[4]);
                                fprintf(out, ",put_asks=[%.2f;%.2f;%.2f;%.2f;%.2f]",
                                        put->asks[0] / 100.0, put->asks[1] / 100.0, put->asks[2] / 100.0, put->asks[3] / 100.0, put->asks[4] / 100.0);
                                fprintf(out, ",put_asks_qty=[%u;%u;%u;%u;%u]",
                                        put->asks_qty[0], put->asks_qty[1], put->asks_qty[2], put->asks_qty[3], put->asks_qty[4]);
                        }

                        break;
                }
                case StrategyKind::CONREV_BID:
                {
                        const StoredMarketDataLatency *fut = nullptr;
                        const StoredMarketDataLatency *call = nullptr;
                        const StoredMarketDataLatency *put = nullptr;
                        fut = &snapshot.data.three_leg_bidding.fut;
                        call = &snapshot.data.three_leg_bidding.call;
                        put = &snapshot.data.three_leg_bidding.put;

                        if (fut && call && put)
                        {
                                // Future data
                                fprintf(out, ",fut_start_time=%llu,fut_seqno=%u,fut_internal_seqno=%u,fut_stream_id=%u,fut_msg_type=%c,fut_ltp=%.2f",
                                        fut->start_time, fut->seqno, fut->internal_seqno, fut->stream_id, fut->msg_type, fut->last_traded_price / 100.0);
                                fprintf(out, ",fut_bids=[%.2f;%.2f;%.2f;%.2f;%.2f]",
                                        fut->bids[0] / 100.0, fut->bids[1] / 100.0, fut->bids[2] / 100.0, fut->bids[3] / 100.0, fut->bids[4] / 100.0);
                                fprintf(out, ",fut_bids_qty=[%u;%u;%u;%u;%u]",
                                        fut->bids_qty[0], fut->bids_qty[1], fut->bids_qty[2], fut->bids_qty[3], fut->bids_qty[4]);
                                fprintf(out, ",fut_asks=[%.2f;%.2f;%.2f;%.2f;%.2f]",
                                        fut->asks[0] / 100.0, fut->asks[1] / 100.0, fut->asks[2] / 100.0, fut->asks[3] / 100.0, fut->asks[4] / 100.0);
                                fprintf(out, ",fut_asks_qty=[%u;%u;%u;%u;%u]",
                                        fut->asks_qty[0], fut->asks_qty[1], fut->asks_qty[2], fut->asks_qty[3], fut->asks_qty[4]);

                                // Call data
                                fprintf(out, ",call_start_time=%llu,call_seqno=%u,call_internal_seqno=%u,call_stream_id=%u,call_msg_type=%c,call_ltp=%.2f",
                                        call->start_time, call->seqno, call->internal_seqno, call->stream_id, call->msg_type, call->last_traded_price / 100.0);
                                fprintf(out, ",call_bids=[%.2f;%.2f;%.2f;%.2f;%.2f]",
                                        call->bids[0] / 100.0, call->bids[1] / 100.0, call->bids[2] / 100.0, call->bids[3] / 100.0, call->bids[4] / 100.0);
                                fprintf(out, ",call_bids_qty=[%u;%u;%u;%u;%u]",
                                        call->bids_qty[0], call->bids_qty[1], call->bids_qty[2], call->bids_qty[3], call->bids_qty[4]);
                                fprintf(out, ",call_asks=[%.2f;%.2f;%.2f;%.2f;%.2f]",
                                        call->asks[0] / 100.0, call->asks[1] / 100.0, call->asks[2] / 100.0, call->asks[3] / 100.0, call->asks[4] / 100.0);
                                fprintf(out, ",call_asks_qty=[%u;%u;%u;%u;%u]",
                                        call->asks_qty[0], call->asks_qty[1], call->asks_qty[2], call->asks_qty[3], call->asks_qty[4]);

                                // Put data
                                fprintf(out, ",put_start_time=%llu,put_seqno=%u,put_internal_seqno=%u,put_stream_id=%u,put_msg_type=%c,put_ltp=%.2f",
                                        put->start_time, put->seqno, put->internal_seqno, put->stream_id, put->msg_type, put->last_traded_price / 100.0);
                                fprintf(out, ",put_bids=[%.2f;%.2f;%.2f;%.2f;%.2f]",
                                        put->bids[0] / 100.0, put->bids[1] / 100.0, put->bids[2] / 100.0, put->bids[3] / 100.0, put->bids[4] / 100.0);
                                fprintf(out, ",put_bids_qty=[%u;%u;%u;%u;%u]",
                                        put->bids_qty[0], put->bids_qty[1], put->bids_qty[2], put->bids_qty[3], put->bids_qty[4]);
                                fprintf(out, ",put_asks=[%.2f;%.2f;%.2f;%.2f;%.2f]",
                                        put->asks[0] / 100.0, put->asks[1] / 100.0, put->asks[2] / 100.0, put->asks[3] / 100.0, put->asks[4] / 100.0);
                                fprintf(out, ",put_asks_qty=[%u;%u;%u;%u;%u]",
                                        put->asks_qty[0], put->asks_qty[1], put->asks_qty[2], put->asks_qty[3], put->asks_qty[4]);
                        }

                        break;
                }
                case StrategyKind::BOX_1_1_1_1:
                {
                        const StoredMarketDataLatency *itm_call = &snapshot.data.box_bidding.long_call;
                        const StoredMarketDataLatency *itm_put = &snapshot.data.box_bidding.long_put;
                        const StoredMarketDataLatency *otm_call = &snapshot.data.box_bidding.short_call;
                        const StoredMarketDataLatency *otm_put = &snapshot.data.box_bidding.short_put;

                        if (itm_call && itm_put && otm_call && otm_put)
                        {
                                // ITM Call data
                                fprintf(out, ",itm_call_start_time=%llu,itm_call_seqno=%u,itm_call_internal_seqno=%u,itm_call_stream_id=%u,itm_call_msg_type=%c,itm_call_ltp=%.2f",
                                        itm_call->start_time, itm_call->seqno, itm_call->internal_seqno, itm_call->stream_id, itm_call->msg_type, itm_call->last_traded_price / 100.0);
                                fprintf(out, ",itm_call_bids=[%.2f;%.2f;%.2f;%.2f;%.2f]",
                                        itm_call->bids[0] / 100.0, itm_call->bids[1] / 100.0, itm_call->bids[2] / 100.0, itm_call->bids[3] / 100.0, itm_call->bids[4] / 100.0);
                                fprintf(out, ",itm_call_bids_qty=[%u;%u;%u;%u;%u]",
                                        itm_call->bids_qty[0], itm_call->bids_qty[1], itm_call->bids_qty[2], itm_call->bids_qty[3], itm_call->bids_qty[4]);
                                fprintf(out, ",itm_call_asks=[%.2f;%.2f;%.2f;%.2f;%.2f]",
                                        itm_call->asks[0] / 100.0, itm_call->asks[1] / 100.0, itm_call->asks[2] / 100.0, itm_call->asks[3] / 100.0, itm_call->asks[4] / 100.0);
                                fprintf(out, ",itm_call_asks_qty=[%u;%u;%u;%u;%u]",
                                        itm_call->asks_qty[0], itm_call->asks_qty[1], itm_call->asks_qty[2], itm_call->asks_qty[3], itm_call->asks_qty[4]);

                                fprintf(out, ",itm_put_start_time=%llu,itm_put_seqno=%u,itm_put_internal_seqno=%u,itm_put_stream_id=%u,itm_put_msg_type=%c,itm_put_ltp=%.2f",
                                        itm_put->start_time, itm_put->seqno, itm_put->internal_seqno, itm_put->stream_id, itm_put->msg_type, itm_put->last_traded_price / 100.0);
                                fprintf(out, ",itm_put_bids=[%.2f;%.2f;%.2f;%.2f;%.2f]",
                                        itm_put->bids[0] / 100.0, itm_put->bids[1] / 100.0, itm_put->bids[2] / 100.0, itm_put->bids[3] / 100.0, itm_put->bids[4] / 100.0);
                                fprintf(out, ",itm_put_bids_qty=[%u;%u;%u;%u;%u]",
                                        itm_put->bids_qty[0], itm_put->bids_qty[1], itm_put->bids_qty[2], itm_put->bids_qty[3], itm_put->bids_qty[4]);
                                fprintf(out, ",itm_put_asks=[%.2f;%.2f;%.2f;%.2f;%.2f]",
                                        itm_put->asks[0] / 100.0, itm_put->asks[1] / 100.0, itm_put->asks[2] / 100.0, itm_put->asks[3] / 100.0, itm_put->asks[4] / 100.0);
                                fprintf(out, ",itm_put_asks_qty=[%u;%u;%u;%u;%u]",
                                        itm_put->asks_qty[0], itm_put->asks_qty[1], itm_put->asks_qty[2], itm_put->asks_qty[3], itm_put->asks_qty[4]);

                                fprintf(out, ",otm_call_start_time=%llu,otm_call_seqno=%u,otm_call_internal_seqno=%u,otm_call_stream_id=%u,otm_call_msg_type=%c,otm_call_ltp=%.2f",
                                        otm_call->start_time, otm_call->seqno, otm_call->internal_seqno, otm_call->stream_id, otm_call->msg_type, otm_call->last_traded_price / 100.0);
                                fprintf(out, ",otm_call_bids=[%.2f;%.2f;%.2f;%.2f;%.2f]",
                                        otm_call->bids[0] / 100.0, otm_call->bids[1] / 100.0, otm_call->bids[2] / 100.0, otm_call->bids[3] / 100.0, otm_call->bids[4] / 100.0);
                                fprintf(out, ",otm_call_bids_qty=[%u;%u;%u;%u;%u]",
                                        otm_call->bids_qty[0], otm_call->bids_qty[1], otm_call->bids_qty[2], otm_call->bids_qty[3], otm_call->bids_qty[4]);
                                fprintf(out, ",otm_call_asks=[%.2f;%.2f;%.2f;%.2f;%.2f]",
                                        otm_call->asks[0] / 100.0, otm_call->asks[1] / 100.0, otm_call->asks[2] / 100.0, otm_call->asks[3] / 100.0, otm_call->asks[4] / 100.0);
                                fprintf(out, ",otm_call_asks_qty=[%u;%u;%u;%u;%u]",
                                        otm_call->asks_qty[0], otm_call->asks_qty[1], otm_call->asks_qty[2], otm_call->asks_qty[3], otm_call->asks_qty[4]);

                                fprintf(out, ",otm_put_start_time=%llu,otm_put_seqno=%u,otm_put_internal_seqno=%u,otm_put_stream_id=%u,otm_put_msg_type=%c,otm_put_ltp=%.2f",
                                        otm_put->start_time, otm_put->seqno, otm_put->internal_seqno, otm_put->stream_id, otm_put->msg_type, otm_put->last_traded_price / 100.0);
                                fprintf(out, ",otm_put_bids=[%.2f;%.2f;%.2f;%.2f;%.2f]",
                                        otm_put->bids[0] / 100.0, otm_put->bids[1] / 100.0, otm_put->bids[2] / 100.0, otm_put->bids[3] / 100.0, otm_put->bids[4] / 100.0);
                                fprintf(out, ",otm_put_bids_qty=[%u;%u;%u;%u;%u]",
                                        otm_put->bids_qty[0], otm_put->bids_qty[1], otm_put->bids_qty[2], otm_put->bids_qty[3], otm_put->bids_qty[4]);
                                fprintf(out, ",otm_put_asks=[%.2f;%.2f;%.2f;%.2f;%.2f]",
                                        otm_put->asks[0] / 100.0, otm_put->asks[1] / 100.0, otm_put->asks[2] / 100.0, otm_put->asks[3] / 100.0, otm_put->asks[4] / 100.0);
                                fprintf(out, ",otm_put_asks_qty=[%u;%u;%u;%u;%u]",
                                        otm_put->asks_qty[0], otm_put->asks_qty[1], otm_put->asks_qty[2], otm_put->asks_qty[3], otm_put->asks_qty[4]);
                        }

                        break;
                }
                case StrategyKind::BOX_2_1_1:
                {
                        const StoredMarketDataLatency *itm_call = &snapshot.data.box_bidding.long_call;
                        const StoredMarketDataLatency *itm_put = &snapshot.data.box_bidding.long_put;
                        const StoredMarketDataLatency *otm_call = &snapshot.data.box_bidding.short_call;
                        const StoredMarketDataLatency *otm_put = &snapshot.data.box_bidding.short_put;

                        if (itm_call && itm_put && otm_call && otm_put)
                        {
                                // ITM Call data
                                fprintf(out, ",itm_call_start_time=%llu,itm_call_seqno=%u,itm_call_internal_seqno=%u,itm_call_stream_id=%u,itm_call_msg_type=%c,itm_call_ltp=%.2f",
                                        itm_call->start_time, itm_call->seqno, itm_call->internal_seqno, itm_call->stream_id, itm_call->msg_type, itm_call->last_traded_price / 100.0);
                                fprintf(out, ",itm_call_bids=[%.2f;%.2f;%.2f;%.2f;%.2f]",
                                        itm_call->bids[0] / 100.0, itm_call->bids[1] / 100.0, itm_call->bids[2] / 100.0, itm_call->bids[3] / 100.0, itm_call->bids[4] / 100.0);
                                fprintf(out, ",itm_call_bids_qty=[%u;%u;%u;%u;%u]",
                                        itm_call->bids_qty[0], itm_call->bids_qty[1], itm_call->bids_qty[2], itm_call->bids_qty[3], itm_call->bids_qty[4]);
                                fprintf(out, ",itm_call_asks=[%.2f;%.2f;%.2f;%.2f;%.2f]",
                                        itm_call->asks[0] / 100.0, itm_call->asks[1] / 100.0, itm_call->asks[2] / 100.0, itm_call->asks[3] / 100.0, itm_call->asks[4] / 100.0);
                                fprintf(out, ",itm_call_asks_qty=[%u;%u;%u;%u;%u]",
                                        itm_call->asks_qty[0], itm_call->asks_qty[1], itm_call->asks_qty[2], itm_call->asks_qty[3], itm_call->asks_qty[4]);

                                fprintf(out, ",itm_put_start_time=%llu,itm_put_seqno=%u,itm_put_internal_seqno=%u,itm_put_stream_id=%u,itm_put_msg_type=%c,itm_put_ltp=%.2f",
                                        itm_put->start_time, itm_put->seqno, itm_put->internal_seqno, itm_put->stream_id, itm_put->msg_type, itm_put->last_traded_price / 100.0);
                                fprintf(out, ",itm_put_bids=[%.2f;%.2f;%.2f;%.2f;%.2f]",
                                        itm_put->bids[0] / 100.0, itm_put->bids[1] / 100.0, itm_put->bids[2] / 100.0, itm_put->bids[3] / 100.0, itm_put->bids[4] / 100.0);
                                fprintf(out, ",itm_put_bids_qty=[%u;%u;%u;%u;%u]",
                                        itm_put->bids_qty[0], itm_put->bids_qty[1], itm_put->bids_qty[2], itm_put->bids_qty[3], itm_put->bids_qty[4]);
                                fprintf(out, ",itm_put_asks=[%.2f;%.2f;%.2f;%.2f;%.2f]",
                                        itm_put->asks[0] / 100.0, itm_put->asks[1] / 100.0, itm_put->asks[2] / 100.0, itm_put->asks[3] / 100.0, itm_put->asks[4] / 100.0);
                                fprintf(out, ",itm_put_asks_qty=[%u;%u;%u;%u;%u]",
                                        itm_put->asks_qty[0], itm_put->asks_qty[1], itm_put->asks_qty[2], itm_put->asks_qty[3], itm_put->asks_qty[4]);

                                fprintf(out, ",otm_call_start_time=%llu,otm_call_seqno=%u,otm_call_internal_seqno=%u,otm_call_stream_id=%u,otm_call_msg_type=%c,otm_call_ltp=%.2f",
                                        otm_call->start_time, otm_call->seqno, otm_call->internal_seqno, otm_call->stream_id, otm_call->msg_type, otm_call->last_traded_price / 100.0);
                                fprintf(out, ",otm_call_bids=[%.2f;%.2f;%.2f;%.2f;%.2f]",
                                        otm_call->bids[0] / 100.0, otm_call->bids[1] / 100.0, otm_call->bids[2] / 100.0, otm_call->bids[3] / 100.0, otm_call->bids[4] / 100.0);
                                fprintf(out, ",otm_call_bids_qty=[%u;%u;%u;%u;%u]",
                                        otm_call->bids_qty[0], otm_call->bids_qty[1], otm_call->bids_qty[2], otm_call->bids_qty[3], otm_call->bids_qty[4]);
                                fprintf(out, ",otm_call_asks=[%.2f;%.2f;%.2f;%.2f;%.2f]",
                                        otm_call->asks[0] / 100.0, otm_call->asks[1] / 100.0, otm_call->asks[2] / 100.0, otm_call->asks[3] / 100.0, otm_call->asks[4] / 100.0);
                                fprintf(out, ",otm_call_asks_qty=[%u;%u;%u;%u;%u]",
                                        otm_call->asks_qty[0], otm_call->asks_qty[1], otm_call->asks_qty[2], otm_call->asks_qty[3], otm_call->asks_qty[4]);

                                fprintf(out, ",otm_put_start_time=%llu,otm_put_seqno=%u,otm_put_internal_seqno=%u,otm_put_stream_id=%u,otm_put_msg_type=%c,otm_put_ltp=%.2f",
                                        otm_put->start_time, otm_put->seqno, otm_put->internal_seqno, otm_put->stream_id, otm_put->msg_type, otm_put->last_traded_price / 100.0);
                                fprintf(out, ",otm_put_bids=[%.2f;%.2f;%.2f;%.2f;%.2f]",
                                        otm_put->bids[0] / 100.0, otm_put->bids[1] / 100.0, otm_put->bids[2] / 100.0, otm_put->bids[3] / 100.0, otm_put->bids[4] / 100.0);
                                fprintf(out, ",otm_put_bids_qty=[%u;%u;%u;%u;%u]",
                                        otm_put->bids_qty[0], otm_put->bids_qty[1], otm_put->bids_qty[2], otm_put->bids_qty[3], otm_put->bids_qty[4]);
                                fprintf(out, ",otm_put_asks=[%.2f;%.2f;%.2f;%.2f;%.2f]",
                                        otm_put->asks[0] / 100.0, otm_put->asks[1] / 100.0, otm_put->asks[2] / 100.0, otm_put->asks[3] / 100.0, otm_put->asks[4] / 100.0);
                                fprintf(out, ",otm_put_asks_qty=[%u;%u;%u;%u;%u]",
                                        otm_put->asks_qty[0], otm_put->asks_qty[1], otm_put->asks_qty[2], otm_put->asks_qty[3], otm_put->asks_qty[4]);
                        }
                        break;
                }
                default:
                        std::cout << "WARNING: WRONG STRATKIND IN UltraLog::format_log function"<< static_cast<int>(snapshot.kind)<<std::endl;
                        break;
                }

                fprintf(out, "\n");
        }
};

int main(int argc, char **argv)
{
        if (argc != 4)
        {
                std::cerr << "Usage: " << argv[0] << " <shm_name> <output_file> <binary|text>" << std::endl;
                std::cerr << "Example: " << argv[0] << " /strategy_shm trades.dat binary" << std::endl;
                return 1;
        }

        const char *shm_name = argv[1];
        const char *output_file = argv[2];
        const char *mode = argv[3];

        signal(SIGINT, signal_handler);
        signal(SIGTERM, signal_handler);

        if (strcmp(mode, "binary") == 0)
        {
                // Binary logging - fastest
                BinaryFileLogger<TradeLog> logger;
                if (!logger.init(shm_name, output_file))
                {
                        return 1;
                }
                logger.run(1024);
        }
        else if (strcmp(mode, "text") == 0)
        {
                // Text logging - human readable
                StrategyDataLog_TextLogger logger;
                if (!logger.init(shm_name, output_file))
                {
                        return 1;
                }
                logger.run(1024);
        }
        else
        {
                std::cerr << "Invalid mode. Use 'binary' or 'text'" << std::endl;
                return 1;
        }

        return 0;
}

/* COMPILATION:
// g++ -O3 -march=native -std=c++17 log_consumer.cpp -o consumer -lrt -lpthread

USAGE:

1. Binary mode (fastest):
   ./consumer /trade_shm trades.dat binary

2. Text mode (CSV format):
   ./consumer_all /strategy_shm_test trades text

To read binary logs:

   #include "shm_logger.hpp"

   int fd = open("trades.dat", O_RDONLY);
   TradeLog log;
   while (read(fd, &log, sizeof(TradeLog)) == sizeof(TradeLog)) {
       // Process log
   }
   close(fd);

To read text logs:
   Just open trades.csv in any text editor or Excel

PERFORMANCE TIPS:
- Binary mode: 5-10M msgs/sec
- Text mode: 1-2M msgs/sec
- Increase batch_size for higher throughput
- Use SSD for log files
- Consider async I/O for extreme throughput
*/

// #include "../Utils/log_shm.hpp"

// int main() {
//     FileLogger<TradeLog> logger;
//     logger.init("/trade_shm", "trades.log");
//     logger.run();
//     return 0;
// }