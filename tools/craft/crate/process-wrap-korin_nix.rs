//! Korin (tools/craft/crate_korin.py) : la petite partie de nix que process-wrap prend, sur libc (nix ne connaît pas
//! Korin). Mêmes noms et mêmes formes : `use crate::korin_nix as nix;` en tête des fichiers qui le nomment.
#![allow(dead_code, non_camel_case_types)]

pub use libc;

pub mod errno {
	/// Un numéro d'erreur (errno), comme `nix::errno::Errno`.
	#[derive(Clone, Copy, Debug, PartialEq, Eq)]
	pub struct Errno(pub i32);

	impl Errno {
		pub const ECHILD: Errno = Errno(libc::ECHILD);
		pub const EINVAL: Errno = Errno(libc::EINVAL);

		pub fn last() -> Errno {
			Errno(std::io::Error::last_os_error().raw_os_error().unwrap_or(0))
		}

		pub fn result(res: i32) -> Result<i32, Errno> {
			if res == -1 { Err(Errno::last()) } else { Ok(res) }
		}
	}

	impl From<Errno> for std::io::Error {
		fn from(e: Errno) -> Self {
			std::io::Error::from_raw_os_error(e.0)
		}
	}
}

pub mod unistd {
	/// Un numéro de processus, comme `nix::unistd::Pid`.
	#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
	pub struct Pid(libc::pid_t);

	impl Pid {
		pub const fn from_raw(pid: libc::pid_t) -> Pid {
			Pid(pid)
		}
		pub const fn as_raw(self) -> libc::pid_t {
			self.0
		}
	}
}

pub mod sys {
	pub mod signal {
		use crate::korin_nix::{errno::Errno, unistd::Pid};

		/// Un signal, par son numéro (`Signal::SIGKILL as i32`).
		#[derive(Clone, Copy, Debug, PartialEq, Eq)]
		#[repr(i32)]
		pub enum Signal {
			SIGHUP = libc::SIGHUP,
			SIGINT = libc::SIGINT,
			SIGQUIT = libc::SIGQUIT,
			SIGKILL = libc::SIGKILL,
			SIGTERM = libc::SIGTERM,
			SIGSTOP = libc::SIGSTOP,
			SIGCONT = libc::SIGCONT,
			SIGUSR1 = libc::SIGUSR1,
			SIGUSR2 = libc::SIGUSR2,
		}

		impl TryFrom<i32> for Signal {
			type Error = Errno;
			fn try_from(n: i32) -> Result<Signal, Errno> {
				use Signal::*;
				[SIGHUP, SIGINT, SIGQUIT, SIGKILL, SIGTERM, SIGSTOP, SIGCONT, SIGUSR1, SIGUSR2]
					.into_iter()
					.find(|s| *s as i32 == n)
					.ok_or(Errno::EINVAL)
			}
		}

		pub fn kill(pid: Pid, sig: Signal) -> Result<(), Errno> {
			Errno::result(unsafe { libc::kill(pid.as_raw(), sig as i32) }).map(drop)
		}

		pub fn killpg(pgrp: Pid, sig: Signal) -> Result<(), Errno> {
			Errno::result(unsafe { libc::killpg(pgrp.as_raw(), sig as i32) }).map(drop)
		}
	}

	pub mod wait {
		/// Les options de waitpid (`bits()` : la valeur de libc).
		#[derive(Clone, Copy, Debug, PartialEq, Eq)]
		pub struct WaitPidFlag(i32);

		impl WaitPidFlag {
			pub const WNOHANG: WaitPidFlag = WaitPidFlag(libc::WNOHANG);
			pub const fn empty() -> WaitPidFlag {
				WaitPidFlag(0)
			}
			pub const fn bits(&self) -> i32 {
				self.0
			}
		}
	}
}

pub mod poll {
	use crate::korin_nix::errno::Errno;
	use std::os::fd::{AsRawFd, BorrowedFd};

	#[derive(Clone, Copy, Debug, PartialEq, Eq)]
	pub struct PollFlags(i16);

	impl PollFlags {
		pub const POLLIN: PollFlags = PollFlags(libc::POLLIN);
		pub fn is_empty(&self) -> bool {
			self.0 == 0
		}
	}

	/// Une entrée de poll (`struct pollfd`).
	pub struct PollFd<'fd> {
		fd: libc::pollfd,
		_fd: std::marker::PhantomData<BorrowedFd<'fd>>,
	}

	impl<'fd> PollFd<'fd> {
		pub fn new(fd: BorrowedFd<'fd>, events: PollFlags) -> PollFd<'fd> {
			PollFd { fd: libc::pollfd { fd: fd.as_raw_fd(), events: events.0, revents: 0 }, _fd: std::marker::PhantomData }
		}
		pub fn revents(&self) -> Option<PollFlags> {
			if self.fd.revents == 0 { None } else { Some(PollFlags(self.fd.revents)) }
		}
	}

	pub struct PollTimeout(i32);

	impl PollTimeout {
		pub const NONE: PollTimeout = PollTimeout(-1);
	}

	pub fn poll(fds: &mut [PollFd<'_>], timeout: PollTimeout) -> Result<i32, Errno> {
		let mut raw: Vec<libc::pollfd> = fds.iter().map(|f| f.fd).collect();
		let r = Errno::result(unsafe { libc::poll(raw.as_mut_ptr(), raw.len() as libc::nfds_t, timeout.0) })?;
		for (f, r) in fds.iter_mut().zip(raw) {
			f.fd.revents = r.revents;
		}
		Ok(r)
	}
}

pub mod fcntl {
	use crate::korin_nix::errno::Errno;
	use std::os::fd::{AsRawFd, BorrowedFd};

	#[derive(Clone, Copy, Debug, PartialEq, Eq)]
	pub struct OFlag(i32);

	impl OFlag {
		pub const O_NONBLOCK: OFlag = OFlag(libc::O_NONBLOCK);
		pub const fn from_bits_truncate(bits: i32) -> OFlag {
			OFlag(bits)
		}
		pub fn set(&mut self, other: OFlag, on: bool) {
			if on { self.0 |= other.0 } else { self.0 &= !other.0 }
		}
	}

	pub enum FcntlArg {
		F_GETFL,
		F_SETFL(OFlag),
	}

	pub fn fcntl(fd: BorrowedFd<'_>, arg: FcntlArg) -> Result<i32, Errno> {
		let fd = fd.as_raw_fd();
		Errno::result(match arg {
			FcntlArg::F_GETFL => unsafe { libc::fcntl(fd, libc::F_GETFL) },
			FcntlArg::F_SETFL(f) => unsafe { libc::fcntl(fd, libc::F_SETFL, f.0) },
		})
	}
}
