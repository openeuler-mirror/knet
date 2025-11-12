Summary:        libknet
Name:           ubs-knet
Version:        1.0.0
Release:        1
License:        Proprietary
Group:          Development/Libraries
Vendor:         Huawei Technologies Co., Ltd.
Requires:       dpstack >= 1.0.0
Autoreq:        no

%define _rpmfilename %%{NAME}-%%{VERSION}.%%{ARCH}.rpm
%define _topdir %(echo $PWD)/build/rpmbuild
%define _rpmdir %{_topdir}/RPMS
%define _srcrpmdir %{_topdir}/SRPMS
%define knetsysdir /etc

%description
knet

%prep

%build

%install
rm -rf %{buildroot}
install -d %{buildroot}"/usr/lib64"
install -d %{buildroot}"/usr/bin"
install -d %buildroot%{knetsysdir}/knet/
install -d %buildroot%{knetsysdir}/knet/run
install -d %buildroot%{knetsysdir}/rsyslog.d/
install -d %buildroot%{knetsysdir}/logrotate.d/

cp -ar %{_topdir}/BUILD/usr/lib64/libknet_core.so* %{buildroot}"/usr/lib64"
cp -ar %{_topdir}/BUILD/usr/lib64/libknet_frame.so* %{buildroot}"/usr/lib64"
cp -ar %{_topdir}/BUILD/usr/lib64/libdpstack.so* %{buildroot}"/usr/lib64"
cp -rf %{_topdir}/BUILD/usr/bin/knet_mp_daemon %{buildroot}"/usr/bin"
cp -rf %{_topdir}/SOURCES/knet_comm.conf %buildroot%{knetsysdir}/knet/
cp -rf %{_topdir}/SOURCES/knet_rsyslog.conf %buildroot%{knetsysdir}/rsyslog.d/
cp -rf %{_topdir}/SOURCES/logrotate/knet %buildroot%{knetsysdir}/logrotate.d/


%pre
if [ -e "/etc/knet/knet_comm.conf" ]; then
    cp -pf /etc/knet/knet_comm.conf /etc/knet/knet_comm.conf.tmp
fi

%post
if [ ! -f /.dockerenv ]; then
    systemctl restart rsyslog
fi

if [ -e "/etc/knet/knet_comm.conf.tmp" ]; then
    diff -q /etc/knet/knet_comm.conf /etc/knet/knet_comm.conf.tmp >/dev/null >&1
    if [ $? -eq 0 ]; then
        rm -f /etc/knet/knet_comm.conf.tmp
    else
        mv /etc/knet/knet_comm.conf.tmp /etc/knet/knet_comm.conf.bak
    fi
fi

%postun
if [ ! -f /.dockerenv ] && [ "$1" = "0" ]; then
    systemctl stop rsyslog
fi

if [[ "$1" = "0" && -e "/etc/knet/knet_comm.conf.bak" ]]; then
    rm -rf /etc/knet/knet_comm.conf.bak
fi

%clean

%files
%defattr(-,root,root,-)

%attr(550, root, root) /usr/lib64/libknet_core.so*
%attr(550, root, root) /usr/lib64/libknet_frame.so*
%attr(550, root, root) /usr/lib64/libdpstack.so*
%attr(550, root, root) /usr/bin/knet_mp_daemon
%attr(600, root, root) %{knetsysdir}/knet/knet_comm.conf
%attr(640, root, root) %{knetsysdir}/rsyslog.d/knet_rsyslog.conf
%attr(640, root, root) %{knetsysdir}/logrotate.d/knet
%dir %{knetsysdir}/knet/run

%changelog