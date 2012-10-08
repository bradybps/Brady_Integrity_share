#if 0 // L&T IES
static inline void device_pm_init(struct device *dev)
{
	dev->power.status = DPM_ON;
}
#endif
// L&T IES
#ifdef CONFIG_PM_RUNTIME

extern void pm_runtime_init(struct device *dev);
extern void pm_runtime_remove(struct device *dev);
/* drivers/base/power/wakeup.c */
//extern unsigned long pm_dev_wakeup_count(struct device *dev);

#else /* !CONFIG_PM_RUNTIME */

static inline void pm_runtime_init(struct device *dev) {}
static inline void pm_runtime_remove(struct device *dev) {}

#endif /* !CONFIG_PM_RUNTIME */

#ifdef CONFIG_PM_SLEEP

/*
 * main.c
 */

extern struct list_head dpm_list;	/* The active device list */

static inline struct device *to_device(struct list_head *entry)
{
	return container_of(entry, struct device, power.entry);
}
extern void device_pm_init(struct device *dev); // L&T IES

extern void device_pm_add(struct device *);
extern void device_pm_remove(struct device *);

#else /* CONFIG_PM_SLEEP */

static inline void device_pm_init(struct device *dev)
{
	pm_runtime_init(dev);
}

static inline void device_pm_remove(struct device *dev)
{
	pm_runtime_remove(dev);
}

static inline void device_pm_add(struct device *dev) {}
//static inline void device_pm_remove(struct device *dev) {} // L&T IES

#endif

#ifdef CONFIG_PM

/*
 * sysfs.c
 */

extern int dpm_sysfs_add(struct device *);
extern void dpm_sysfs_remove(struct device *);

#else /* CONFIG_PM */

static inline int dpm_sysfs_add(struct device *dev)
{
	return 0;
}

static inline void dpm_sysfs_remove(struct device *dev)
{
}

#endif
