# Parallel Order Book Matching Engine

**Kevin Tang - kktang**, **Spencer Fisher - smfisher**

Project Site: [Parallel-Order-Book](https://github.com/s033Fish/15-418-Parallel-Order-Book)

## Summary:

Our goal is to build a sequential order book matching engine in C++ to increase performance by incorporating parallelism on multicore CPUs. Beyond the obvious split of parallelizing by stock, we will parallelize orders within a specific stock while producing trades identical to a sequential matching engine. We will evaluate the performance of our engine with realistic order submission data from NASDAQ ITCH’s data, which is a live historical data set with real trades from the exchange.

## Background:

An order book is a live list of open buy and sell orders for a specific financial asset. Matching engines use order books as a ledger to pair buyers and sellers whenever there is an overlap in prices. 

![Order Book Example](images/orderbook_example.png)

In this example, we can see that there is an overlap between the highest buy order and the lowest sell order, which means that the matching engine would need to process these orders.

Exchanges, such as NASDAQ and NYSE, are responsible for hosting and managing the electronic order books where the orders reside. High-Frequency trading (HFT) firms rely on microsecond-level speed to execute various strategies. As such, an exchange is motivated to achieve maximal performance of its order book matching engines to attract more active HFT traders to maximize exchange revenue and market liquidity.

Matching engines use various algorithms to process orders, but the two most common algorithms are pro-rata and price-time. The main difference between the two algorithms is how they fill orders at a certain price level. A pro-rata order book fills proportionally based on size. So each sell order at the same price sells (buy order quantity) * (sell order quantity) / (total sell order quantity at price) units. A price-time order book instead fills by arrival timestamp. This means it will fill sell orders at a certain price that came first before filling any later orders at that price.


## Challenges:

The main challenge with parallelizing an order book is the numerous dependencies in the matching algorithms. Both matching algorithms are inherently sequential, as the outcome of order i depends on the state left by orders 0 through i-1. In addition, to maintain fairness across placed orders, orders need to be processed in the sequence that they are received by the exchange, which again is inherently sequential. For price-time based matching algorithms, the filling process for each price level is conceptually sequential, as the amount of remaining stocks for a sell order depends on how many orders were filled of all of the earlier orders for that price.

Workload imbalance also presents additional challenges, as certain stocks are traded more frequently than others. Most transactions also occur at the top of the order book, which gives rise to difficulty in parallelization across prices. This also creates memory access issues, as these price levels will be accessed frequently, leading to heavy cache contention.

The main constraint is that the performance of the parallel algorithm must operate in the exact same manner as the sequential order, so there can be no reordering of transactions.

## Resources:

We’ll conduct our testing on both GHC and PSC machines. We would like to use the PSC machines since stock exchanges typically process massive loads of incoming transactions, which we believe would be better simulated on PSC’s 128-core nodes, where we can stress-test scalability under heavy load beyond what GHC’s 8 cores allow.

## Goals and Deliverables:

Plan to Achieve:
- Implement a correct sequential matching engine as well as an automated correctness checker to compare output between versions
- A per-stock parallelized baseline that splits matching based on specific stocks
- Compare data structure locking implementations (fine-grained, coarse-grained, lock-free)
- Implement a within-stock parallelized matching engine that speeds up pro-rata and price-time algorithms

Hope to Achieve:
- Be able to support buy stop and sell stop orders with our matching engine
- Experiment with a fairness-relaxing and batch auction matching engine to compare how performance changes when we allow for higher parallelism but weaker fairness

## Platform:

Order books are traditionally implemented with C++ since matching speed comes down to memory layout, cache behavior, and synchronization cost, and C++ lets you control all three directly.

## Schedule:

Week 1: Sequential implementation, testing different data structures to identify benchmarks. Create a correctness and performance testing platform based on NASDAQ ITCH’s data.

Week 2: Complete across-stock parallel implementation and compare various workload-assigning types (dynamic vs. static vs. semi-static)

Week 3: Complete initial within-stock parallel implementation

Week 4: Optimize within-stock parallel implementation for memory layout, contention, etc. to achieve maximum performance

Week 5: Gather performance comparison data on GHC and PSC and complete the final project report
