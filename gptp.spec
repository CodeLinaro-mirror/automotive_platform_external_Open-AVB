Name: gptp
Version: 1.0
Release: r0
Summary: Time Sensitive Networking stack Time Sync.
License: BSD-3-Clause & BSD-2-Clause & BSD-3-Clause-Clear
URL: https://www.codelinaro.org/
Source0: %{name}-%{version}.tar.gz

BuildRequires: autoconf automake libtool gcc-g++

%description
Time Sensitive Networking stack Time Sync.

%prep
%autosetup -n %{name}-%{version}

%build
make gptp libgptp libgptp_test

%install
mkdir -p %{buildroot}/%{_bindir}
mkdir -p %{buildroot}/%{_libdir}
mkdir -p %{buildroot}/%{_includedir}
install -m 755 daemons/gptp/linux/build/obj/qgptp %{buildroot}/%{_bindir}
install -m 755 examples/libgptp_test/libgptp_test %{buildroot}/%{_bindir}
install -m 755 lib/libgptp/*.so %{buildroot}/%{_libdir}
install -m 644 lib/libgptp/gptp_helper.h %{buildroot}/%{_includedir}

%files
%{_bindir}/qgptp
%{_bindir}/libgptp_test
%{_libdir}/libgptp.so
%{_includedir}/gptp_helper.h
