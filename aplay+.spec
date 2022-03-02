%define name aplay+
%define version 0.6
%define release b1

Name:		%{name}
Summary:	a simple BitPerfect player
Version:	%{version}
Release:	%{release}
License:	GPL
Group:		Applications/Multimedia
Source:	%{name}-%{version}.tar.bz2
Buildroot:	%{_tmppath}/%{name}-%{version}
#alsa-lib-devel

%description
a simple BitPerfect player

%prep
%setup -q

%build
make

%install
mkdir -p %{buildroot}/usr/bin
strip aplay+
install -m 755 aplay+ %{buildroot}/usr/bin

%clean
[ -n "%{buildroot}" -a "%{buildroot}" != / ] && rm -rf %{buildroot}
rm -rf $RPM_BUILD_DIR/%{name}-%{version}

%files
%defattr (-,root,root)
/usr/bin/*

%changelog
* Tue Mar 12 2019 Yuichiro Nakada <berry@berry-lab.net>
- Create for Berry Linux
