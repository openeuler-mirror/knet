# K-NET

## What's New

- [2026-03-30]: Released K-Network (K-NET) to the openEuler community for the first time.

## Introduction

The traditional kernel protocol stack processes network data packets through CPU interrupts, TCP/IP protocol stack processing, and cross-state copies. As a result, the performance of received and sent packets is low, which cannot meet the high-performance forwarding requirements of services. Data Plane Development Kit (DPDK) addresses these issues by providing the high-speed forwarding capability and implementing Layer 2 protocol forwarding. However, it cannot parse the TCP/IP protocol stack. When applications are building on DPDK for service acceleration, the following must be implemented from scratch for service interconnection: Layer 2 to Layer 4 protocols, Socket APIs, and the event handling capability.
To address this issue, the K-NET framework is introduced. Based on Kunpeng NICs, K-NET enables imperceptible application migration. K-NET provides a high-performance user-mode protocol stack and standard Socket APIs, enabling imperceptible service adaptation. Services can leverage the high-performance K-NET protocol stack for rapid adaptation and performance acceleration.
K-NET is designed as a flexible and high-performance protocol framework that evolves with future network demands while balancing performance and usability in network scenarios. The following table shows the K-NET acceleration effect in Redis scenarios.

| Scenario      | Concurrency | Packet Length| Kernel Value   | K-NET Value  |
|------------|-------|------|-----------|-----------|
| redis get  | 100   | Default size of redis-benchmark  | 194886.19 | 540511.31 |
| redis set  | 100   | Default size of redis-benchmark   | 169064.58 | 434008.94 |
| redis get  | 1000  | Default size of redis-benchmark| 161480.45 | 505816.66 |
| redis set  | 1000  | Default size of redis-benchmark   | 141693.23 | 376067.09 |

For details, see [K-NET Product Description](./docs/zh/product_descritions/descritions_menu.md).

## Release Notes

The release nodes for K-NET include the version mapping and feature changes of each version. For details, see [Release Notes](./docs/zh/release_note.md).

## Source Code Download

You can use either of the following methods to download the K-NET source code:

```shell
$ git clone https://atomgit.com/openeuler/knet.git
```

## Source Code Directory Structure

```shell
.
├── cmake      // Stores build dependencies.
├── conf       // Stores initial configuration items.
├── demo       // Stores demo examples.
├── docs       // Document description.
├── opensource // Stores project dependencies.
├── package    // Stores the RPM package build configuration.
├── src        // Stores the source code for implementing project functions. Only this directory is involved in package building.
├── test       // Stores the UT and SDV test files of the project.
└── build.py   // Unified build entrance.
```

## Quick Start

The quick start is designed for K-NET beginners. It allows you to quickly run an instance to experience the acceleration capabilities of K-NET.
For details, see [Quick Start](./docs/zh/quick_start.md).

## Installation and Deployment

For details about the networking planning, environment setup, installation and deployment of K-NET, see [Installation](./docs/zh/installation/installation.md).

## Feature Use

For details about how to use the K-NET acceleration feature and precautions, see [Feature Guide](./docs/zh/feature_guide/feature_menu.md).

### Hardware Support

K-NET supports Huawei SP670 NICs. You can obtain the NIC driver and firmware from the Support website.

### Supported Features

- Single-process Model Acceleration
- Multi-process Model Acceleration
- Traffic Bifurcation
- User-Space Virtual Bonding
- Kernel Traffic Forwarding
- Zero Copy
- Thread Sharing

## APIs

For details about APIs, see [APIs](./docs/zh/api/api_menu.md).

## Reference

For appendix references, see [References](./docs/zh/reference/reference_menu.md).

## Troubleshooting

For details about troubleshooting, see [Troubleshooting](./docs/zh/troubleshooting/troubleshooting_menu.md).

## License

K-NET is licensed under the Mulan Permissive Software License, Version 2 (Mulan PSL v2).

## Contribution

Read the contribution guide [CONTRIBUTING.md](./CONTRIBUTING.md) to learn how to contribute to the project.

## Suggestions and Feedback

You are welcome to contribute to the community. If you have any questions or suggestions, submit issues. We will reply as soon as possible. Thank you for your support.
