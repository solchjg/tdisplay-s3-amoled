package WaterMonitor::Store;

# The local archive in SQLite: the same file and tables as the Python
# version (water_monitor/store.py), so either can read what the other wrote.
# Times are ISO 8601 UTC strings ('2026-11-11T20:23:22+00:00').

use strict;
use warnings;
use DBI;
use JSON::PP;
use POSIX qw(strftime);
use Time::Local qw(timegm);
use WaterMonitor::Report qw(to_utc_epoch);

my $JSON = JSON::PP->new->canonical(1);

my $SCHEMA = <<'SQL';
CREATE TABLE IF NOT EXISTS reports (
    serial TEXT NOT NULL,
    report_key TEXT NOT NULL,
    report_no INTEGER,
    location TEXT,
    email_utc TEXT,
    last_sample_utc TEXT,
    battery_pct REAL,
    bat_v REAL,
    signal INTEGER,
    state TEXT,
    logger_state TEXT,
    logger_logs TEXT,
    clock_lag_min REAL,
    extra TEXT,
    PRIMARY KEY (serial, report_key)
);
CREATE TABLE IF NOT EXISTS samples (
    serial TEXT NOT NULL,
    ts_utc TEXT NOT NULL,
    values_json TEXT NOT NULL,
    PRIMARY KEY (serial, ts_utc)
)
SQL

sub iso { defined $_[0] ? strftime('%Y-%m-%dT%H:%M:%S+00:00', gmtime $_[0]) : undef }

sub epoch_of {
    my ($text) = @_;
    return undef unless defined $text;
    $text =~ /^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d{2})(?:\.\d+)?([+-]\d{2}):?(\d{2})$/
        or die "bad time '$text'\n";
    my $e = timegm($6, $5, $4, $3, $2 - 1, $1);
    my $off = (abs($7) * 3600 + $8 * 60) * (substr($7, 0, 1) eq '-' ? -1 : 1);
    return $e - $off;
}

sub new {
    my ($class, $path) = @_;
    my $db = DBI->connect("dbi:SQLite:dbname=$path", '', '',
                          {RaiseError => 1, AutoCommit => 1, sqlite_unicode => 1});
    $db->do($_) for split /;\s*\n/, $SCHEMA;
    return bless {db => $db}, $class;
}

sub close { $_[0]{db}->disconnect }

# Store a parsed report; returns {new_report, new_samples, clock_lag_min}.
sub add {
    my ($self, $r, $offset) = @_;
    my $db = $self->{db};
    my @rows = map { [to_utc_epoch($_->{local_time}, $offset), $_->{values}] }
               @{ $r->{samples} };
    my ($last) = sort { $b <=> $a } map { $_->[0] } @rows;
    my $lag = defined $r->{email_epoch} ? ($r->{email_epoch} - $last) / 60 : undef;
    my $key = defined $r->{report_no} ? "$r->{report_no}"
            : iso($r->{email_epoch} // $last);
    my $lf = sub { WaterMonitor::Report::logger_field($r, @_) };
    my $extra = $JSON->encode({
        usb_v => $r->{usb_v}, lat => $r->{lat}, lon => $r->{lon},
        messages => $r->{messages}, logger_serial => $lf->('serial'),
        logger_battery => $lf->('battery'), logger_type => $lf->('type'),
    });
    $db->begin_work;
    my $new_report = $db->do(
        'INSERT OR IGNORE INTO reports VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?)',
        undef, $r->{serial}, $key, $r->{report_no}, $r->{location},
        iso($r->{email_epoch}), iso($last), $r->{battery_pct}, $r->{bat_v},
        $r->{signal}, $r->{state}, $lf->('state'), $lf->('total logs'),
        $lag, $extra) + 0;
    my $new = 0;
    my $ins = $db->prepare('INSERT OR IGNORE INTO samples VALUES (?,?,?)');
    for my $row (@rows) {
        $new += $ins->execute($r->{serial}, iso($row->[0]),
                              $JSON->encode($row->[1])) + 0;
    }
    $db->commit;
    return {new_report => $new_report, new_samples => $new, clock_lag_min => $lag};
}

sub serials {
    my ($self) = @_;
    return @{ $self->{db}->selectcol_arrayref(
        'SELECT DISTINCT serial FROM reports ORDER BY serial') };
}

# [[epoch, {values}], ...] oldest first.
sub samples {
    my ($self, $serial) = @_;
    my $rows = $self->{db}->selectall_arrayref(
        'SELECT ts_utc, values_json FROM samples WHERE serial=? ORDER BY ts_utc',
        undef, $serial);
    return [map { [epoch_of($_->[0]), $JSON->decode($_->[1])] } @$rows];
}

# Report rows as hashes, oldest first; times as epoch seconds.
sub reports {
    my ($self, $serial) = @_;
    my $rows = $self->{db}->selectall_arrayref(
        'SELECT * FROM reports WHERE serial=? ORDER BY last_sample_utc',
        {Slice => {}}, $serial);
    for my $r (@$rows) {
        $r->{email_epoch} = epoch_of($r->{email_utc});
        $r->{last_sample_epoch} = epoch_of($r->{last_sample_utc});
        $r->{extra} = $JSON->decode($r->{extra} || '{}');
    }
    return $rows;
}

1;
