BIN  = baseapp
SRCS =						\
	archiver				\
	entity					\
	entity_component		\
	baseapp					\
	baseapp_interface		\
	backuper				\
	entity_messages_forward_handler		\
	data_download			\
	data_downloads			\
	entity_autoloader		\
	entity_remotemethod		\
	forward_message_over_handler		\
	initprogress_handler				\
	restore_entity_handler				\
	sync_entitystreamtemplate_handler	\
	space					\
	main					\
	proxy					\
	proxy_forwarder			\
	profile

ASMS =

MY_LIBS =		\
	server		\
	entitydef	\
	pyscript	\
	network		\
	thread
	

BUILD_TIME_FILE = main
USE_PYTHON = 1
USE_G3DMATH=1
USE_OPENSSL = 1

ifndef NO_USE_LOG4CXX
	NO_USE_LOG4CXX = 0
	CPPFLAGS += -DLOG4CXX_STATIC
endif

CPPFLAGS += -DOURO_BASEAPP

#HAS_PCH = 1
USE_JWSMTP = 1

ifndef OURO_ROOT
export OURO_ROOT := $(subst /ouro/src/server/$(BIN),,$(CURDIR))
endif

include $(OURO_ROOT)/ouro/src/build/common.mak

