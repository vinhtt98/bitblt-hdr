ifndef rax
.686
.model flat, stdcall
endif

.code

do_proxy macro func
	ifdef rax
		extern &func&_Original:QWORD
		&func&_EXPORT proc
			jmp QWORD ptr &func&_Original
		&func&_EXPORT endp
	else
		extern &func&_Original:DWORD
		&func&_EXPORT proc
			jmp DWORD ptr &func&_Original
		&func&_EXPORT endp
	endif
endm

do_proxy GetFileVersionInfoA
do_proxy GetFileVersionInfoByHandle
do_proxy GetFileVersionInfoExA
do_proxy GetFileVersionInfoExW
do_proxy GetFileVersionInfoSizeA
do_proxy GetFileVersionInfoSizeExA
do_proxy GetFileVersionInfoSizeExW
do_proxy GetFileVersionInfoSizeW
do_proxy GetFileVersionInfoW
do_proxy VerFindFileA
do_proxy VerFindFileW
do_proxy VerInstallFileA
do_proxy VerInstallFileW
do_proxy VerLanguageNameA
do_proxy VerLanguageNameW
do_proxy VerQueryValueA
do_proxy VerQueryValueW

end

