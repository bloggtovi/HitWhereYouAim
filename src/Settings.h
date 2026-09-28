#pragma once

enum NotificationMode
{
	None,
	Floating,
	Screen,
	Both
};

struct Settings
{
	static void Load();
};

