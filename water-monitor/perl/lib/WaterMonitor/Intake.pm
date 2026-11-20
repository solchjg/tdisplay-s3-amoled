package WaterMonitor::Intake;

# One email in, stored: the step the mailbox poller repeats, and the alerts
# it raises (a gap in report numbers, a low battery, a wrong clock offset).

use strict;
use warnings;
use Email::MIME;
use WaterMonitor::Report qw(parse rfc2822_epoch);
use Exporter 'import';

our @EXPORT_OK = qw(report_from_bytes process late_sites);

sub _plain_body {
    my ($mime) = @_;
    my ($found, @html);
    $mime->walk_parts(sub {
        my ($p) = @_;
        return if $found || $p->subparts;
        my $ct = $p->content_type // 'text/plain';
        my $text = eval { $p->body_str };
        if (!defined $text) {   # a wrong or missing charset: take UTF-8
            $text = $p->body;
            utf8::decode($text);
        }
        if ($ct =~ m{^text/plain}i) { $found = $text }
        elsif ($ct =~ m{^text/html}i) { push @html, $text }
    });
    return $found // $html[0] // '';
}

# A report from RFC 822 bytes or plain report text. In a forward, the
# original's Subject and Date in the body win over the forwarding message's.
sub report_from_bytes {
    my ($raw) = @_;
    my $mime = eval { Email::MIME->new($raw) };
    if ($mime && $mime->header('From') && $mime->header('Date')) {
        my $r = parse(_plain_body($mime));
        $r->{email_epoch} //= rfc2822_epoch($mime->header('Date'));
        $r->{subject} = $mime->header('Subject') // '' unless length $r->{subject};
        return $r;
    }
    my $text = $raw;
    utf8::decode($text);
    return parse($text);
}

# {ok, serial, report_no, new_samples, reason, alerts => [...]}
sub process {
    my ($raw, $cfg, $store) = @_;
    my $r = eval { report_from_bytes($raw) };
    if (!$r) {
        (my $why = $@) =~ s/^report: |\s+$//g;
        return {ok => 0, reason => $why};
    }
    my $site = $cfg->{sites}{ $r->{serial} };
    return {ok => 0, serial => $r->{serial}, report_no => $r->{report_no},
            reason => "unknown site $r->{serial}: add it to the config with "
                    . "its clock's utc_offset"} unless $site;
    my $added = $store->add($r, $site->{offset});
    my $title = "$site->{serial} $site->{name}" =~ s/\s+$//r;
    my @alerts;
    my @nums = sort { $a <=> $b } grep { defined }
               map { $_->{report_no} } @{ $store->reports($r->{serial}) };
    if (defined $r->{report_no}) {
        my ($prev) = reverse grep { $_ < $r->{report_no} } @nums;
        push @alerts, "$title: report(s) "
            . join(', ', $prev + 1 .. $r->{report_no} - 1) . ' never arrived'
            if defined $prev && $prev < $r->{report_no} - 1;
    }
    push @alerts, sprintf('%s: LevelSender battery %g%%', $title, $r->{battery_pct})
        if defined $r->{battery_pct} && $r->{battery_pct} < $cfg->{battery_low_pct};
    my $lag = $added->{clock_lag_min};
    push @alerts, sprintf('%s: email time is %+.0f min from the last sample; '
                          . "check the site's utc_offset", $title, $lag)
        if defined $lag && ($lag < -15 || $lag > 180);
    return {ok => 1, serial => $r->{serial}, report_no => $r->{report_no},
            new_samples => $added->{new_samples}, alerts => \@alerts};
}

# Sites with no report for late_hours.
sub late_sites {
    my ($store, $cfg, $now) = @_;
    my @out;
    for my $serial (@{ $cfg->{site_order} }) {
        my $reports = $store->reports($serial);
        next unless @$reports;
        my $last = $reports->[-1]{email_epoch} // $reports->[-1]{last_sample_epoch};
        my $hours = ($now - $last) / 3600;
        my $site = $cfg->{sites}{$serial};
        push @out, sprintf('%s: no report for %.0f h',
                           "$serial $site->{name}" =~ s/\s+$//r, $hours)
            if $hours > $cfg->{late_hours};
    }
    return @out;
}

1;
