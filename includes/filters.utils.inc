<?php


/**
 * @file
 * A filter to run some basic utility functions. Basically any useful option not big enough to justify it's own class.
 */

/**
 * A filter to run some basic utility functions.
 *
 * @ingroup backup_migrate_filters
 */
class backup_migrate_filter_utils extends backup_migrate_filter {
  public $op_weights = array('pre_backup' => -1000, 'post_backup' => 1000);
  public $saved_devel_query = NULL;


  /**
   * Flag to indicate we have put the site in maintenance mode.
   * 
   * @var bool $saved_site_offline
   */
  public $saved_site_offline;

  /**
   * Temporarily stores original maintenance mode message.
   */
  public $saved_site_offline_message;


  /**
   * Get the default backup settings for this filter.
   */
  function backup_settings_default() {
    return array(
      'utils_disable_query_log' => TRUE,
      'utils_site_offline' => FALSE,
      'utils_description' => '',
    );
  }

  /**
   * Get the default restore settings for this filter.
   */
  function restore_settings_default() {
    return array(
      'utils_disable_query_log' => TRUE,
      'utils_site_offline' => FALSE,
    );
  }

  /**
   * Get the form for the backup settings for this filter.
   */
  function backup_settings_form($settings) {
    $form = array();
    if (module_exists('devel') && config_get('backup_migrate.settings','dev_query')) {
      $form['database']['utils_disable_query_log'] = array(
        '#type' => 'checkbox',
        '#title' => t('Disable query log'),
        '#default_value' => !empty($settings['utils_disable_query_log']) ? $settings['utils_disable_query_log'] : NULL,
        '#description' => t('Disable the devel module\'s query logging during the backup operation. It will be enabled again after backup is complete. This is very highly recommended.'),
      );
    }
    $form['advanced']['utils_site_offline'] = array(
      '#type' => 'checkbox',
      '#title' => t('Take site offline'),
      '#default_value' => !empty($settings['utils_site_offline']) ? $settings['utils_site_offline'] : NULL,
      '#description' => t('Take the site offline during backup and show a maintenance message. Site will be taken back online once the backup is complete.'),
    );
    $form['advanced']['utils_site_offline_message_wrapper'] = array(
      '#type' => 'backup_migrate_dependent',
      '#dependencies' => array(
        'filters[utils_site_offline]' => TRUE,
      ),
    );
    $form['advanced']['utils_site_offline_message_wrapper']['utils_site_offline_message'] = array(
      '#type' => 'textarea',
      '#title' => t('Site off-line message'),
      '#default_value' => !empty($settings['utils_site_offline_message']) ? $settings['utils_site_offline_message'] : config_get('backup_migrate.settings','site_offline_message'),
      '#description' => t('Message to show visitors when the site is in off-line mode.'),
    );
    $form['advanced']['utils_description'] = array(
      '#type' => 'textarea',
      '#title' => t('Add a note'),
      '#default_value' => !empty($settings['utils_description']) ? $settings['utils_description'] : NULL,
      '#description' => t('Add a short note to the backup file.'),
    );
    $form['advanced']['utils_drop_all_tables'] = array(
      '#type' => 'checkbox',
      '#title' => t('Drop all tables before import (MySQL only)'),
      '#default_value' => !empty($settings['utils_drop_all_tables']) ? $settings['utils_drop_all_tables'] : NULL,
      '#description' => t('Drop all existing database tables before restoring the backup. This option is currently available on MySQL servers only.'),
    );
    $form['advanced']['use_cli'] = array(
      "#type" => "checkbox",
      "#title" => t("Use cli commands"),
      "#default_value" => !empty($settings['use_cli']),
      "#description" => t("Use the command line tools (mysqldump, tar, gzip etc.) if available. This can be faster for large sites but will not work on all servers. EXPERIMENTAL"),
    );
    $form['advanced']['ignore_errors'] = array(
      "#type" => "checkbox",
      "#title" => t("Ignore errors"),
      "#default_value" => !empty($settings['ignore_errors']),
      "#description" => t("Will attempt to complete backup even if certain recoverable errors occur. This may make the backup files invalid. Enable this if you have unreadable files that you want to ignore during backup."),
    );

    return $form;
  }

  /**
   * Get the form for the restore settings for this filter.
   */
  function restore_settings_form($settings) {
    $form = array();
    if (module_exists('devel') && config_get('backup_migrate.settings','dev_query')) {
      $form['advanced']['utils_disable_query_log'] = array(
        '#type' => 'checkbox',
        '#title' => t('Disable query log'),
        '#default_value' => @$settings['utils_disable_query_log'],
        '#description' => t('Disable the devel module\'s query logging during the restore operation. It will be enabled again after restore is complete. This is very highly recommended.'),
      );
    }
    $form['advanced']['utils_site_offline'] = array(
      '#type' => 'checkbox',
      '#title' => t('Take site offline'),
      '#default_value' => !empty($settings['utils_site_offline']) ? $settings['utils_site_offline'] : NULL,
      '#description' => t('Take the site offline during restore and show a maintenance message. Site will be taken back online once the restore is complete.'),
    );
    $form['advanced']['utils_site_offline_message_wrapper'] = array(
      '#type' => 'backup_migrate_dependent',
      '#dependencies' => array(
        'filters[utils_site_offline]' => TRUE,
      ),
    );
    $form['advanced']['utils_site_offline_message_wrapper']['utils_site_offline_message'] = array(
      '#type' => 'textarea',
      '#title' => t('Site off-line message'),
      '#default_value' => !empty($settings['utils_site_offline_message']) ? $settings['utils_site_offline_message'] : config_get('backup_migrate.settings','site_offline_message'),
      '#description' => t('Message to show visitors when the site is in off-line mode.')
    );
    $form['advanced']['use_cli'] = array(
      "#type" => "checkbox",
      "#title" => t("Use cli commands"),
      "#default_value" => !empty($settings['use_cli']),
      "#description" => t("Use the command line tools (mysqldump, tar, gzip etc.) if available. This can be faster for large sites but will not work on all servers. EXPERIMENTAL"),
    );
    // $form['advanced']['ignore_errors'] = array(
    //   "#type" => "checkbox",
    //   "#title" => t("Ignore errors"),
    //   "#default_value" => !empty($settings['ignore_errors']),
    //   "#description" => t("Will attempt to complete restore even if certain recoverable errors occur. This may could corrupt your site."),
    // );
    return $form;
  }

  function pre_backup($file, $settings) {
    $this->take_site_offline($settings);
    $this->disable_devel_query($settings);
  }

  function post_backup($file, $settings) {
    $this->enable_devel_query($settings);
    $this->take_site_online($settings);
    if ($file) {
      $this->add_file_info($file, $settings);    
    }
  }

  function pre_restore($file, $settings) {
    $this->disable_devel_query($settings);
    $this->take_site_offline($settings);
  }

  function post_restore($file, $settings) {
    $this->enable_devel_query($settings);
    $this->take_site_online($settings);
  }

  /**
   * Disable devel query logging if it's active and the user has chosen to do so.
   */
  function disable_devel_query($settings) {
    $this->saved_devel_query = config_get('backup_migrate.settings','dev_query');
    if (module_exists('devel') && config_get('backup_migrate.settings','dev_query') && !empty($settings->filters['utils_disable_query_log'])) {
      config_set('backup_migrate.settings','dev_query', 0);
    }
  }

  /**
   * Restore devel query to previous state.
   */
  function enable_devel_query($settings) {
    if (module_exists('devel')) {
      config_set('backup_migrate.settings','dev_query', $this->saved_devel_query);
    }
  }

  /**
   * Add the backup metadata to the file.
   */
  function add_file_info($file, $settings) {
    $file->file_info['description']       = $settings->filters['utils_description'];
    $file->file_info['datestamp']         = time();
    $file->file_info['generator']         = 'Backup and Migrate for Backdrop (https://github.com/backdrop-contrib/backup_migrate)';
    $file->file_info['generatorversion']  = BACKUP_MIGRATE_VERSION;
    $file->file_info['distro']            = 'Backdrop';
    $file->file_info['distroversion']     = BACKDROP_VERSION;
    $file->file_info['siteurl']           = url('', array('absolute' => TRUE));
    $file->file_info['sitename']          = config_get('system.core','site_name');
    $file->file_info['backdropversion']     = BACKDROP_VERSION;
    $file->calculate_filesize();

    $source = $settings->get('source');
    $file->file_info['bam_sourceid']      = $source->get('id');
    $file->file_info['bam_sourcetype']    = $source->get('subtype');
    $file->file_info['bam_sourcename']    = $source->get('name');

    // Add any additional info that has been added to the settings by other plugins.
    if (!empty($settings->file_info)) {
      $file->file_info += $settings->file_info;
    }
  }

  /**
   * Take the site offline if configured to do so.
   */
  function take_site_offline($settings) {
    // Save the current state of the site in case a restore overwrites it.
    $this->saved_site_offline = config_get('backup_migrate.settings','maintenance_mode');
    if (@$settings->filters['utils_site_offline']) {
      $this->saved_site_offline_message = config_get('backup_migrate.settings','maintenance_mode_message');
      if (!empty($settings->filters['utils_site_offline_message'])) {
        // Store system message temporarily.
        $this->saved_site_offline_message = config_get('backup_migrate.settings','maintenance_mode_message');
        config_set('backup_migrate.settings','maintenance_mode_message', $settings->filters['utils_site_offline_message']);
      }
      config_set('backup_migrate.settings','maintenance_mode', 1);
      _backup_migrate_message('Site was taken offline.');
    }
  }

  /**
   * Take the site online again after backup or restore.
   */
  function take_site_online($settings) {
    // Take the site back off/online because the restored db may have changed that setting.
    if (config_get('backup_migrate.settings','maintenance_mode') == 0 && !empty($this->saved_site_offline)) {
      config_set('backup_migrate.settings','maintenance_mode', 0);
      if ($settings->filters['utils_site_offline']) {
        if (!empty($this->saved_site_offline_message)) {
          config_set('backup_migrate.settings','maintenance_mode_message', $this->saved_site_offline_message);
        }
        _backup_migrate_message('Site was taken online.');
      }
    }
  }
}
