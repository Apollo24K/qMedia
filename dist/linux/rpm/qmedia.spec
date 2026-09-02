Name:		qmedia
Version:	6.1
Release:	1
Summary:	Minimal image and video viewer

Group:		Productivity/Graphics/Viewers
License:	GPL-3.0-or-later
URL:		https://github.com/Apollo24K/qMedia
Source:	    https://github.com/Apollo24K/qMedia/archive/refs/tags/%{version}.tar.gz

BuildRequires: cmake >= 3.16
BuildRequires: pkgconfig
BuildRequires: pkgconfig(Qt5Widgets) >= 5.9
BuildRequires: pkgconfig(Qt5Network) >= 5.9
BuildRequires: pkgconfig(Qt5Multimedia) >= 5.9
BuildRequires: pkgconfig(Qt5MultimediaWidgets) >= 5.9
BuildRequires: pkgconfig(Qt5X11Extras) >= 5.9


%description
qMedia is a lightweight Qt image and video viewer based on qView.

%prep
%autosetup -n qMedia-%{version}

%build
%cmake
%cmake_build

%install
%cmake_install

%files
/usr/bin/*
/usr/share/icons/*
/usr/share/applications/*
/usr/share/metainfo/*
%license LICENSE
%doc README.md

%changelog
