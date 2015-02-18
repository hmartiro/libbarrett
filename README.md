# libbarrett #

This is a Git fork of official
[libbarrett SVN repository](http://web.barrett.com/svn/barrett-ros-pkg/trunk/)
maintained by
[Barrett Technology's](http://www.barrett.com/). See `README.orig` for the
original documentation provided with libbarrett. This `README` file includes
updated installation instructions for Ubuntu 14.04 and Ubuntu 12.04 on the
amd64 architecture.

## Installation Instructions ##

### Ubuntu 12.04 ###

Install system dependencies:

```bash
apt-get install libeigen2-dev
```

The version of libconfig++ shipped with Ubuntu 12.04 is too old to be used by
libbarrett. Additionally, libbarrett requires a modified version of libconfig++
that adds a public `getCSettings` method.

We will build this patched version of libconfig from source:

```bash
wget http://web.barrett.com/svn/libbarrett/dependencies/libconfig-1.4.5-PATCHED.tar.gz
tar xzf libconfig-1.4.5-PATCHED.tar.gz
cd libconfig-1.4.5
debuild -i -us -uc -b
```

This will generate a collection of `.deb` files in teh parent directory. You
should install using the following command:

```bash
sudo dpkg -i ../libconfig*.deb
```

### Ubuntu 14.04 ###

Install system dependencies:

```bash
apt-get install libconfig-dev libconfig++-dev  libeigen2-dev # Ubuntu 14.04
```
Libbarrett requires a modified version of libconfig++ that adds a public
`getCSettings` method. You need to modify `/usr/include/libconfig.h++` by a
`public:` block:

```c++
// BARRETT(DC): Added this inline method to allow simultaneous libconfig and
//              libconfig++ use.
config_setting_t *getCSetting() const { return(_setting); }
```

### Building ###

Finally, you can build libbarrett from source:

```bash
mkdir build && cd build
cmake -DNON_REALTIME:bool=true ..
make
```

This assumes that you are running the standard Ubuntu kernel. If you want
real-time guarantees, you should install Xenomai and set the `NON_REALTIME`
flag to `false`.
